/* FCE Ultra - NES/Famicom Emulator
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */
//
// BasicBlockView.cpp
//
#include "Qt/BasicBlockView.h"

#include <ios>
#include <iostream>
#include <qgraphicsview.h>
#include <qnamespace.h>
#include <set>
#include <stack>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <QActionGroup>
#include <QColorDialog>
#include <QFileDialog>
#include <QFontDialog>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QGraphicsView>
#include <QHeaderView>
#include <QInputDialog>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QSpinBox>
#include <QTableWidget>
#include <SDL.h>

// Must be before the following. Keep a space here so clang-format
// doesn't reorder them
#include "../../fceu.h"

#include "../../asm.h"
#include "../../debug.h"
#include "../../fds.h"
#include "../../palette.h"
#include "../../ppu.h"
#include "../../x6502.h"

#include "Qt/fceuWrapper.h"
#include "Qt/keyscan.h"

static constexpr int TRACK_HEIGHT = 10;
static constexpr bool DEBUG_GRAPH = false; // Set to true for debugging

static QPainterPath makeManhattanPath(const std::vector<QPointF> &points,
                                      qreal radius = 10.0)
{
	QPainterPath path;
	if (points.empty()) return path;
	if (points.size() == 1)
	{
		path.moveTo(points[0]);
		return path;
	}

	path.moveTo(points[0]);

	for (size_t i = 1; i < points.size(); ++i)
	{
		const QPointF &prev = points[i - 1];
		const QPointF &curr = points[i];

		if (i == points.size() - 1)
		{
			// Last segment - no curve needed
			path.lineTo(curr);
		}
		else
		{
			const QPointF &next = points[i + 1];

			// Calculate the direction vectors
			QPointF toCurr = curr - prev;
			QPointF toNext = next - curr;

			qreal distToCurr =
				std::sqrt(toCurr.x() * toCurr.x() + toCurr.y() * toCurr.y());
			qreal distToNext =
				std::sqrt(toNext.x() * toNext.x() + toNext.y() * toNext.y());

			// Limit radius to half the shorter segment
			qreal actualRadius =
				std::min(radius, std::min(distToCurr, distToNext) / 2.0);

			// Draw line to point before curve
			QPointF beforeCurve = curr - (toCurr / distToCurr) * actualRadius;
			path.lineTo(beforeCurve);

			// Draw arc around the corner
			QPointF afterCurve = curr + (toNext / distToNext) * actualRadius;
			path.quadTo(curr, afterCurve);
		}
	}

	return path;
}

struct Instruction
{
	uint16 address;
	uint8 opcode[3];
	std::string pretty;

	static Instruction atAddress(uint16 addr)
	{
		Instruction insn{
			.address = addr,
			.opcode = {0, 0, 0},
			.pretty = "INVALID",
		};
		int size = opsize[GetMem(addr)];
		for (int i = 0; i < size; ++i)
		{
			insn.opcode[i] = GetMem(addr + i);
		}
		insn.pretty = Disassemble(addr + size, insn.opcode);
		return insn;
	}

	std::string toString() const { return pretty; }

	uint8 size() const { return opsize[opcode[0]]; }

	std::vector<uint16> next() const
	{
		std::vector<uint16> result;
		switch (opcode[0])
		{
		case 0x4C: // JMP (Absolute)
		case 0x20: // JSR
			result.push_back(opcode[1] | (opcode[2] << 8));
			break;
		case 0x6C: // JMP (Indirect)
		{
			uint16 ptr = opcode[1] | (opcode[2] << 8);
			result.push_back(GetMem(ptr) | (GetMem(ptr + 1) << 8));
		}
		break;
		case 0x60: // RTS (ReTurn from Subroutine)
		case 0x40: // RTI (ReTurn from Interrupt)
			// No statically-known successors. We need to walk the whole
			// program to find callers
			break;
		case 0x10: // BPL (Branch on PLus)
		case 0x30: // BMI (Branch on MInus)
		case 0x50: // BVC (Branch on oVerflow Clear)
		case 0x70: // BVS (Branch on oVerflow Set)
		case 0x90: // BCC (Branch on Carry Clear)
		case 0xB0: // BCS (Branch on Carry Set)
		case 0xD0: // BNE (Branch on Not Equal)
		case 0xF0: // BEQ (Branch on EQual)
			// Branch is relative to next address, and signed
			result.push_back(address + size());
			result.push_back(address + size() + static_cast<int8>(opcode[1]));
			break;
		default: // Non-jumps
			result.push_back(address + size());
			break;
		}
		return result;
	}
};

std::ostream &operator<<(std::ostream &o, const Instruction &insn)
{
	return o << insn.pretty;
}

struct BasicBlock
{
	std::vector<Instruction> instructions;

	static BasicBlock fromAddress(uint16 addr)
	{
		BasicBlock bb;
		while (1)
		{
			bb.instructions.push_back(Instruction::atAddress(addr));
			std::vector<uint16> next = bb.instructions.back().next();
			if (next.size() != 1)
			{
				break;
			}
			addr = next[0];
		}
		return bb;
	}

	uint16 address() const { return instructions.front().address; }
	std::vector<uint16> next() const { return instructions.back().next(); }
};

std::ostream &operator<<(std::ostream &o, const BasicBlock &bb)
{
	auto flags = o.flags();
	auto fill = o.fill();

	o << std::hex << std::uppercase << std::setfill('0');

	o << "BB<" << std::setw(4) << bb.address() << ">\n";
	for (const Instruction &insn : bb.instructions)
	{
		o << "[" << std::setw(4) << insn.address << "]: " << insn << "\n";
	}
	o << "Next: ";

	bool first = true;
	for (uint16 next : bb.next())
	{
		if (!first)
		{
			o << " | ";
		}
		o << std::setw(4) << next;
		first = false;
	}

	o.flags(flags);
	o.fill(fill);
	return o;
}

struct BasicBlockSet
{
	uint16 entry;
	std::map<uint16, BasicBlock> basicBlocks;

	const BasicBlock &at(uint16 addr) const { return basicBlocks.at(addr); }

	static BasicBlockSet fromAddress(uint16 entry)
	{
		std::stack<uint16> addresses;
		addresses.push(entry);
		BasicBlockSet bbSet{.entry = entry};

		while (!addresses.empty())
		{
			const uint16 addr = addresses.top();
			addresses.pop();
			const BasicBlock bb = BasicBlock::fromAddress(addr);
			bbSet.basicBlocks.insert({addr, bb});
			for (uint16 nextAddr : bb.next())
			{
				if (bbSet.basicBlocks.count(nextAddr) == 0)
				{
					addresses.push(nextAddr);
				}
			}
		}
		return bbSet;
	}
};

unsigned LayerInfo::overTracks() const { return overTrackCount; }

qreal LayerInfo::overTrackAreaHeight() const
{
	return TRACK_HEIGHT * overTracks();
}

unsigned LayerInfo::underTracks() const { return underTrackCount; }

qreal LayerInfo::underTrackAreaHeight() const
{
	return TRACK_HEIGHT * underTracks();
}

qreal LayerInfo::nodeAreaHeight() const
{
	qreal height = 0;
	for (const Node &node : nodes)
	{
		height = std::max(height, node.size().height());
	}
	return height;
}

qreal LayerInfo::layerHeight() const
{
	return overTrackAreaHeight() + nodeAreaHeight() + underTrackAreaHeight();
}

bool EdgeInfo::needsOverTrack() const
{
	if (source.layer < target.get().layer)
	{
		return false;
	}
	else if (source.layer > target.get().layer)
	{
		return true;
	}
	else
	{
		if (dynamic_cast<const DummyNode *>(&source))
		{
			assert(!dynamic_cast<const DummyNode *>(&target.get()));
			return true;
		}
		else
		{
			assert(dynamic_cast<const DummyNode *>(&target.get()));
			return false;
		}
	}
}

bool EdgeInfo::needsUnderTrack() const { return !needsOverTrack(); }

DummyNode::DummyNode(QPointF pos)
	: QGraphicsRectItem(pos.x(), pos.y(), DEBUG_GRAPH ? 10 : 0,
                        DEBUG_GRAPH ? 10 : 0)
{
	if (!DEBUG_GRAPH)
	{
		setPen(Qt::NoPen);
		setBrush(Qt::NoBrush);
	}
}

// Returns max layer assigned
unsigned GraphView::computeLayers(Node &node, unsigned layer)
{
	if (node.layer)
	{
		return *node.layer;
	}
	node.layer = layer;
	unsigned maxLayer = layer;
	for (const EdgeInfo edge : node.nexts)
	{
		maxLayer = std::max(maxLayer, computeLayers(edge.target, layer + 1));
	}
	return maxLayer;
}

GraphView::GraphView(const BasicBlockSet &bbSet, QWidget *parent)
	: QGraphicsView(parent)
{
	setScene(&scene_);
	setDragMode(QGraphicsView::ScrollHandDrag);
	setRenderHint(QPainter::Antialiasing, true);

	// Make nodes
	for (const auto &[addr, bb] : bbSet.basicBlocks)
	{
		BasicBlockNode *bbItem = new BasicBlockNode(bb);
		scene_.addItem(bbItem);
		nodes.push_back(bbItem);
		addrToNode.insert({addr, bbItem});
	}

	// Add edges
	for (const auto &[addr, bb] : bbSet.basicBlocks)
	{
		for (uint16 nextAddr : bb.next())
		{
			Node *node = addrToNode.at(addr);
			node->nexts.push_back(EdgeInfo{
				.source = *node,
				.target = *addrToNode.at(nextAddr),
				.track = 0,
			});
		}
	}

	// Pick layers
	unsigned maxLayer = computeLayers(*addrToNode.at(bbSet.entry));

	// Add dummy nodes
	for (size_t fromIdx = 0; fromIdx < nodes.size(); ++fromIdx)
	{
		Node *from = nodes[fromIdx];
		if (dynamic_cast<DummyNode *>(from)) continue;
		for (size_t nextIdx = 0; nextIdx < from->nexts.size(); ++nextIdx)
		{
			Node &to = from->nexts[nextIdx].target;
			if (dynamic_cast<DummyNode *>(&to)) continue;

			Node *lastFrom = from;
			if (*from->layer < *to.layer)
			{
				for (int layer = *from->layer + 1; layer < *to.layer; ++layer)
				{
					DummyNode *dummy = new DummyNode(QPointF(0, 0));
					nodes.push_back(dummy);
					scene_.addItem(dummy);
					dummy->layer = layer;
					if (lastFrom->nexts.size() > 0)
					{
						// It is the starting node, and we should swap the
						// "next" we are currently looking at
						lastFrom->nexts[nextIdx].target = std::ref(*dummy);
					}
					else
					{
						lastFrom->nexts.push_back(EdgeInfo{
							.source = *lastFrom,
							.target = std::ref(*dummy),
						});
					}
					lastFrom = dummy;
				}
				// We did insert a dummy (because nexts == {}), add the final
				// connection to the "to"
				if (lastFrom->nexts.size() == 0)
					lastFrom->nexts.push_back({
						.source = *lastFrom,
						.target = to,
					});
			}
			else // Previous layer
			{
				// Here we add dummies on the start and end layers too
				for (int layer = *from->layer; layer >= *to.layer; --layer)
				{
					DummyNode *dummy = new DummyNode(QPointF(0, 0));
					nodes.push_back(dummy);
					scene_.addItem(dummy);
					dummy->layer = layer;
					if (!DEBUG_GRAPH)
					{
						dummy->setPen(Qt::NoPen);
						dummy->setBrush(Qt::NoBrush);
					}
					if (lastFrom->nexts.size() > 0)
					{
						// It is the starting node, and we should swap the
						// "next" we are currently looking at
						lastFrom->nexts[nextIdx].target = std::ref(*dummy);
					}
					else
					{
						lastFrom->nexts.push_back({
							.source = *lastFrom,
							.target = std::ref(*dummy),
						});
					}
					lastFrom = dummy;
				}
				// We did insert a dummy (because nexts == {}), add the final
				// connection to the "to"
				if (lastFrom->nexts.size() == 0)
					lastFrom->nexts.push_back({
						.source = *lastFrom,
						.target = to,
					});
			}
		}
	}

	// Merge dummy nodes that share the same target
	std::set<Node *> nodesToRemove;
	for (unsigned layer = 0; layer <= maxLayer; ++layer)
	{
		// Build a map from target to dummy nodes pointing to it
		std::map<Node *, std::vector<DummyNode *>> targetToDummies;
		for (Node *node : nodes)
		{
			if (nodesToRemove.count(node)) continue;
			DummyNode *dummy = dynamic_cast<DummyNode *>(node);
			if (!dummy || !dummy->layer || *dummy->layer != layer) continue;
			if (dummy->nexts.size() == 1)
			{
				Node &target = dummy->nexts[0].target;
				targetToDummies[&target].push_back(dummy);
			}
		}

		// Merge dummies pointing to the same target
		for (auto &[target, dummies] : targetToDummies)
		{
			if (dummies.size() <= 1) continue;

			// Keep the first dummy, merge others into it
			DummyNode *kept = dummies[0];
			for (size_t i = 1; i < dummies.size(); ++i)
			{
				DummyNode *toRemove = dummies[i];

				// Redirect all incoming edges to point to the kept dummy
				for (Node *node : nodes)
				{
					if (nodesToRemove.count(node)) continue;
					for (EdgeInfo &edge : node->nexts)
					{
						if (&edge.target.get() == toRemove)
						{
							edge.target = std::ref(*kept);
						}
					}
				}

				// Mark for removal
				nodesToRemove.insert(toRemove);
				scene_.removeItem(toRemove);
			}
		}
	}

	// Remove merged nodes from the nodes vector
	nodes.erase(std::remove_if(nodes.begin(), nodes.end(),
	                           [&nodesToRemove](Node *n)
	                           { return nodesToRemove.count(n) > 0; }),
	            nodes.end());

	// Delete the removed nodes
	for (Node *node : nodesToRemove)
	{
		delete node;
	}

	// Create layers
	layers.resize(maxLayer + 1);
	for (Node *node : nodes)
	{
		assert(node->layer); // All layers should be decided at this point
		layers.at(*node->layer).nodes.push_back(*node);
	}

	// Sort horizontally. Downwards dummies on the left, then blocks, then
	// upwards on the right
	for (LayerInfo &layer : layers)
	{
		std::sort(layer.nodes.begin(), layer.nodes.end(),
		          [](const Node &nodeA, const Node &nodeB)
		          {
					  // Categorize nodes: 0 = downwards dummy, 1 = block, 2 =
			          // upwards dummy
					  auto getCategory = [](const Node &node) -> int
					  {
						  const DummyNode *dummy =
							  dynamic_cast<const DummyNode *>(&node);
						  if (!dummy) return 1; // Regular block

						  if (dummy->nexts.empty()) return 1;

						  const Node &target = dummy->nexts[0].target.get();
						  if (*target.layer > *dummy->layer)
							  return 0; // Downwards
						  if (*target.layer < *dummy->layer)
							  return 2; // Upwards
						  return 1;     // Same layer (treat as regular)
					  };

					  int catA = getCategory(nodeA);
					  int catB = getCategory(nodeB);

					  return catA < catB;
				  });

		// Second pass: float self-loop dummy to the right of its parent
		// Assuming only one self-loop per layer
		std::map<const Node *, Node *> parentToSelfLoopDummy;
		for (Node &node : layer.nodes)
		{
			for (const EdgeInfo &edge : node.nexts)
			{
				Node &target = edge.target.get();
				// Same-layer edge to a dummy indicates a self-loop structure
				if (target.layer == node.layer &&
				    dynamic_cast<DummyNode *>(&target) &&
				    &target.nexts.at(0).target.get() == &node)
				{
					parentToSelfLoopDummy[&node] = &target;
				}
			}
		}

		// Reorder: for each parent, move its self-loop dummy to immediately
		// follow it
		std::vector<std::reference_wrapper<Node>> reordered;
		std::set<const Node *> processed;

		for (Node &node : layer.nodes)
		{
			if (processed.count(&node)) continue;

			reordered.push_back(std::ref(node));
			processed.insert(&node);

			// Add the self-loop dummy if this node has one
			auto it = parentToSelfLoopDummy.find(&node);
			if (it != parentToSelfLoopDummy.end())
			{
				Node *dummy = it->second;
				if (!processed.count(dummy))
				{
					reordered.push_back(std::ref(*dummy));
					processed.insert(dummy);
				}
			}
		}

		layer.nodes = reordered;
	}

	// Place horizontally: space out blocks within each layer
	const unsigned BLOCK_GAP = 20;
	for (const LayerInfo &layer : layers)
	{
		qreal x = 0;
		for (Node &node : layer.nodes)
		{
			node.setX(x);
			x += node.size().width() + BLOCK_GAP;
		}
	}

	// Assign tracks based on horizontal span overlap
	// Edges can share a track if their horizontal spans don't intersect
	// or if they share the same source or target
	for (LayerInfo &layer : layers)
	{
		// Collect edges with their horizontal spans
		struct EdgeSpan
		{
			EdgeInfo *edge;
			qreal sourceX, targetX, minX, maxX;
			bool isOver;
		};
		std::vector<EdgeSpan> edgeSpans;

		for (Node &fromNode : layer.nodes)
		{
			for (EdgeInfo &edge : fromNode.nexts)
			{
				const Node &toNode = edge.target.get();
				qreal fromX = fromNode.pos().x() + fromNode.size().width() / 2;
				qreal toX = toNode.pos().x() + toNode.size().width() / 2;
				edgeSpans.push_back({
					.edge = &edge,
					.sourceX = fromX,
					.targetX = toX,
					.minX = std::min(fromX, toX),
					.maxX = std::max(fromX, toX),
					.isOver = edge.needsOverTrack(),
				});
			}
		}

		// Assign tracks using interval scheduling
		// Track = list of (sourceX, targetX, minX, maxX) tuples for edges on
		// that track
		struct TrackInterval
		{
			qreal sourceX, targetX, minX, maxX;
		};
		std::vector<std::vector<TrackInterval>> overTracksIntervals;
		std::vector<std::vector<TrackInterval>> underTracksIntervals;

		for (EdgeSpan &span : edgeSpans)
		{
			auto &tracks =
				span.isOver ? overTracksIntervals : underTracksIntervals;

			// Find first track where this edge doesn't conflict
			bool assigned = false;
			for (size_t trackIdx = 0; trackIdx < tracks.size(); ++trackIdx)
			{
				bool conflicts = false;
				for (const TrackInterval &existing : tracks[trackIdx])
				{
					// Edges can overlap if they share the same source or target
					bool sameSource =
						std::abs(span.sourceX - existing.sourceX) < 0.1;
					bool sameTarget =
						std::abs(span.targetX - existing.targetX) < 0.1;
					if (sameSource || sameTarget)
					{
						continue; // Same endpoint, allow overlap
					}
					// Check if [span.minX, span.maxX] overlaps with
					// [existing.minX, existing.maxX]
					if (!(span.maxX < existing.minX ||
					      span.minX > existing.maxX))
					{
						conflicts = true;
						break;
					}
				}
				if (!conflicts)
				{
					span.edge->track = trackIdx;
					tracks[trackIdx].push_back(
						{span.sourceX, span.targetX, span.minX, span.maxX});
					assigned = true;
					break;
				}
			}

			// If no existing track works, create a new one
			if (!assigned)
			{
				span.edge->track = tracks.size();
				tracks.push_back(
					{{span.sourceX, span.targetX, span.minX, span.maxX}});
			}
		}

		// Cache the track counts
		layer.overTrackCount = overTracksIntervals.size();
		layer.underTrackCount = underTracksIntervals.size();
	}

	// Place vertically (after track assignment so we have correct track counts)
	const unsigned LAYER_GAP = 20;
	for (Node *node : nodes)
	{
		qreal yPos = 0;
		for (unsigned i = 0; i < *node->layer; ++i)
		{
			yPos += layers[i].layerHeight() + LAYER_GAP;
		}
		yPos += layers[*node->layer].overTrackAreaHeight();
		node->setY(yPos);
	}

	// Draw edges
	for (const Node *fromNode : nodes)
	{
		for (const EdgeInfo &nextEdge : fromNode->nexts)
		{
			const Node &toNode = nextEdge.target;
			const unsigned track = nextEdge.track;
			const LayerInfo &fromLayer = layers.at(*fromNode->layer);
			const QPointF fromPos = fromNode->pos();
			const QSizeF fromSize = fromNode->size();
			const QPointF toPos = toNode.pos();
			const QSizeF toSize = toNode.size();
			const QPointF fromTop = fromPos + QPointF(fromSize.width() / 2, 0);
			const QPointF fromBottom = fromTop + QPointF(0, fromSize.height());
			const QPointF toTop = toPos + QPointF(toSize.width() / 2, 0);
			const QPointF toBottom = toTop + QPointF(0, toSize.height());

			QPainterPath path;
			if (*fromNode->layer < *toNode.layer)
			{
				// Downwards
				const qreal trackY = fromPos.y() + fromLayer.nodeAreaHeight() +
				                     TRACK_HEIGHT * (1 + track);
				path = makeManhattanPath({
					fromBottom,
					{fromBottom.x(), trackY},
					{toTop.x(), trackY},
					toTop,
				});
			}
			else if (*fromNode->layer > *toNode.layer)
			{
				assert(dynamic_cast<const DummyNode *>(fromNode));
				// Upwards (to previous layer)
				const qreal trackY = fromPos.y() - TRACK_HEIGHT * (1 + track);
				path = makeManhattanPath({
					fromTop,
					{fromTop.x(), trackY},
					{toBottom.x(), trackY},
					toBottom,
				});
			}
			else if (*fromNode->layer == *toNode.layer)
			{
				// Same layer
				if (dynamic_cast<const DummyNode *>(fromNode))
				{
					assert(!dynamic_cast<const DummyNode *>(&toNode));
					const qreal trackY =
						fromPos.y() - TRACK_HEIGHT * (1 + track);
					path = makeManhattanPath({
						fromTop,
						{fromTop.x(), trackY},
						{toTop.x(), trackY},
						toTop,
					});
				}
				else
				{
					assert(dynamic_cast<const DummyNode *>(&toNode));
					const qreal trackY = fromPos.y() +
					                     fromLayer.nodeAreaHeight() +
					                     TRACK_HEIGHT * (1 + track);
					path = makeManhattanPath({
						fromBottom,
						{fromBottom.x(), trackY},
						{toBottom.x(), trackY},
						toBottom,
					});
				}
			}
			else
			{
				// We have inserted dummy nodes to break layer crossings, so
				// these should be the only cases
				assert(0 && "Unreachable");
			}

			const auto color =
				DEBUG_GRAPH
					? (fromNode->layer < toNode.layer ? Qt::black : Qt::red)
					: Qt::black;
			auto *pathItem = scene_.addPath(path, QPen(color, 2));
			pathItem->setZValue(10);
		}
	}
}

static BasicBlockView_t *basicBlockViewWin = NULL;
//----------------------------------------------------------------------------
void openBasicBlockViewWindow(QWidget *parent, int force)
{
	if (!force)
	{
		if (basicBlockViewWin != NULL) return;
	}
	basicBlockViewWin = new BasicBlockView_t(parent);
	basicBlockViewWin->show();
}
//----------------------------------------------------------------------------
BasicBlockView_t::BasicBlockView_t(QWidget *parent)
	: QDialog(parent, Qt::Window)
{
	/* Load data */
	uint16 reset, irq, nmi;
	FCEUI_GetIVectors(&reset, &irq, &nmi);
	const BasicBlockSet bbs = BasicBlockSet::fromAddress(reset);

	QSettings settings;
	QVBoxLayout *mainLayout;

	setWindowTitle("Basic Block View");

	resize(512, 512);

	mainLayout = new QVBoxLayout();

	graphView_ = new GraphView(bbs, this);
	mainLayout->addWidget(graphView_);

	setLayout(mainLayout);

	basicBlockViewWin = this;

	restoreGeometry(settings.value("basicBlockView/geometry").toByteArray());
}
//----------------------------------------------------------------------------
BasicBlockView_t::~BasicBlockView_t(void)
{
	QSettings settings;

	if (basicBlockViewWin == this)
	{
		basicBlockViewWin = NULL;
	}
	settings.setValue("basicBlockView/geometry", saveGeometry());
}
//----------------------------------------------------------------------------
void BasicBlockView_t::closeEvent(QCloseEvent *event)
{
	QSettings settings;
	settings.setValue("basicBlockView/geometry", saveGeometry());
	done(0);
	deleteLater();
	event->accept();
}
//----------------------------------------------------------------------------
void BasicBlockView_t::closeWindow(void)
{
	QSettings settings;
	settings.setValue("basicBlockView/geometry", saveGeometry());
	done(0);
	deleteLater();
}
//----------------------------------------------------------------------------
BasicBlockNode::BasicBlockNode(const BasicBlock &bb, QGraphicsItem *parent)
	: QGraphicsProxyWidget(parent), bb_(bb)
{
	std::ostringstream oss;
	oss << bb;
	QString text = QString::fromStdString(oss.str());

	QLabel *label = new QLabel(text);
	label->setFont(QFont("monospace"));
	label->setContentsMargins(6, 4, 6, 4);
	// clang-format off
	label->setStyleSheet(
		"background-color: white;"
		"padding: 2px;"
		"border: 2px solid #ccc;"
	);
	// clang-format on

	setWidget(label);
	setAcceptHoverEvents(true);
}

void BasicBlockNode::hoverEnterEvent(QGraphicsSceneHoverEvent *event)
{
	if (QLabel *label = qobject_cast<QLabel *>(widget()))
	{
		// clang-format off
		label->setStyleSheet(
			"background-color: #e3f2fd;"
			"padding: 2px;"
			"border: 2px solid #2196f3;"
		);
		// clang-format on
	}
	QGraphicsProxyWidget::hoverEnterEvent(event);
}

void BasicBlockNode::hoverLeaveEvent(QGraphicsSceneHoverEvent *event)
{
	if (QLabel *label = qobject_cast<QLabel *>(widget()))
	{
		// clang-format off
		label->setStyleSheet(
			"background-color: white;"
			"padding: 2px;"
			"border: 2px solid #ccc;"
		);
		// clang-format on
	}
	QGraphicsProxyWidget::hoverLeaveEvent(event);
}
