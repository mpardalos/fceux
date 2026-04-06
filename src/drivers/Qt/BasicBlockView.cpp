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
#include "../../cart.h"
#include "../../debug.h"
#include "../../fds.h"
#include "../../ines.h"
#include "../../palette.h"
#include "../../ppu.h"
#include "../../x6502.h"

#include "Qt/fceuWrapper.h"
#include "Qt/keyscan.h"

constexpr int TRACK_HEIGHT = 10;

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
			result.push_back(address + size() + static_cast<int8>(opcode[1]));
			result.push_back(address + size());
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

qreal GraphView::LayerInfo::nodeAreaHeight() const
{
	qreal height = 0;
	for (const Node &node : nodes)
	{
		height = std::max(height, node.size().height());
	}
	return height;
}

qreal GraphView::LayerInfo::trackAreaHeight() const
{
	qreal height = 0;
	for (const Node &node : nodes)
	{
		for (const EdgeInfo &edge : node.nexts)
		{
			height += TRACK_HEIGHT;
		}
	}
	return height;
}

qreal GraphView::LayerInfo::layerHeight() const
{
	return nodeAreaHeight() + trackAreaHeight();
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
			addrToNode.at(addr)->nexts.push_back(
				EdgeInfo{.target = *addrToNode.at(nextAddr), .track = 0});
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
						lastFrom->nexts.push_back({std::ref(*dummy), 0});
					}
					lastFrom = dummy;
				}
				// We did insert a dummy (because nexts == {}), add the final
				// connection to the "to"
				if (lastFrom->nexts.size() == 0)
					lastFrom->nexts.push_back({.target = to, .track = 0});
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
					if (lastFrom->nexts.size() > 0)
					{
						// It is the starting node, and we should swap the
						// "next" we are currently looking at
						lastFrom->nexts[nextIdx].target = std::ref(*dummy);
					}
					else
					{
						lastFrom->nexts.push_back({std::ref(*dummy), 0});
					}
					lastFrom = dummy;
				}
				// We did insert a dummy (because nexts == {}), add the final
				// connection to the "to"
				if (lastFrom->nexts.size() == 0)
					lastFrom->nexts.push_back({.target = to, .track = 0});
			}
		}
	}

	// Create layers
	layers.resize(maxLayer + 1);
	for (Node *node : nodes)
	{
		assert(node->layer); // All layers should be decided at this point
		layers.at(*node->layer).nodes.push_back(*node);
	}

	// Place vertically
	const unsigned LAYER_GAP = 20;
	for (Node *node : nodes)
	{
		unsigned yPos = 0;
		for (unsigned i = 0; i < *node->layer; ++i)
		{
			yPos += layers[i].layerHeight() + LAYER_GAP;
		}
		node->setY(yPos);
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

	// Assign tracks
	for (LayerInfo &layer : layers)
	{
		unsigned tracks = 0;
		for (Node &node : layer.nodes)
		{
			for (EdgeInfo &edge : node.nexts)
			{
				edge.track = tracks++;
			}
		}
	}

	// Draw edges
	for (const Node *fromNode : nodes)
	{
		for (const EdgeInfo &nextEdge : fromNode->nexts)
		{
			const Node &toNode = nextEdge.target;
			const unsigned track = nextEdge.track;
			const LayerInfo &fromLayer = layers.at(*fromNode->layer);
			const LayerInfo &toLayer = layers.at(*toNode.layer);
			const QPointF fromPos = fromNode->pos();
			const QSizeF fromSize = fromNode->size();
			const QPointF toPos = toNode.pos();
			const QSizeF toSize = toNode.size();
			const bool fromIsDummy = dynamic_cast<const DummyNode *>(fromNode);
			const bool toIsDummy = dynamic_cast<const DummyNode *>(&toNode);
			const QPointF fromTop = fromPos + QPointF(fromSize.width() / 2, 0);
			const QPointF fromBottom = fromTop + QPointF(0, fromSize.height());
			const QPointF toTop = toPos + QPointF(toSize.width() / 2, 0);
			const QPointF toBottom = toTop + QPointF(0, toSize.height());

			QPainterPath path;
			// if (fromNode == toNode)
			// {
			// 	// Self-loop: curve out to the right and back
			// 	qreal w = fromSize.width();
			// 	qreal h = fromSize.height();
			// 	QPointF rightEdge = fromPos + QPointF(w + 40, h / 2);
			// 	path.cubicTo(rightEdge + QPointF(0, h / 2),
			// 				 rightEdge + QPointF(0, -h / 2), toPt);
			// }
			if (*fromNode->layer < *toNode.layer)
			{
				// Downwards
				QPointF pt = fromBottom;
				path.moveTo(pt);
				// Down to the track section
				pt.ry() = fromPos.y() + fromLayer.nodeAreaHeight() +
						  TRACK_HEIGHT * (1 + track);
				path.lineTo(pt);
				// Sideways to the target
				pt.rx() = toTop.x();
				path.lineTo(pt);
				// Down to the target
				path.lineTo(toTop);
			}
			else if (*fromNode->layer == *toNode.layer)
			{
				// Same layer
				if (dynamic_cast<const DummyNode *>(fromNode))
				{
					assert(!dynamic_cast<const DummyNode *>(&toNode));
					QPointF pt = fromTop;
					path.moveTo(pt);
					// Up a bit (needs a proper track)
					pt.ry() -= 10;
					path.lineTo(pt);
					// Sideways to the target
					pt.rx() = toTop.x();
					path.lineTo(pt);
					// Into the target (from the top)
					path.lineTo(toTop);
				}
				else
				{
					assert(dynamic_cast<const DummyNode *>(&toNode));
					QPointF pt = fromBottom;
					path.moveTo(pt);
					// Down to the track section
					pt.ry() = fromPos.y() + fromLayer.nodeAreaHeight() +
							  TRACK_HEIGHT * (1 + track);
					path.lineTo(pt);
					// Sideways to the target
					pt.rx() = toBottom.x();
					path.lineTo(pt);
					// Into the target (from the bottom)
					path.lineTo(toBottom);
				}
			}
			else if (*fromNode->layer > *toNode.layer)
			{
				assert(dynamic_cast<const DummyNode *>(fromNode));
				// Upwards (to previous layer)
				QPointF pt = fromTop;
				path.moveTo(pt);
				// Up a bit (should have a proper track)
				pt.ry() -= 10;
				path.lineTo(pt);
				// Sideways to the target
				pt.rx() = toBottom.x();
				path.lineTo(pt);
				// Into the target (from the bottom)
				path.lineTo(toBottom);
			}
			else
			{
				// We have inserted dummy nodes to break layer crossings, so
				// these should be the only cases
				assert(0 && "Unreachable");
			}

			auto *pathItem = scene_.addPath(
				path,
				QPen(fromNode->layer < toNode.layer ? Qt::black : Qt::red, 2));
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
GraphView::BasicBlockNode::BasicBlockNode(const BasicBlock &bb,
										  QGraphicsItem *parent)
	: QGraphicsProxyWidget(parent), bb_(bb)
{
	std::ostringstream oss;
	oss << bb;
	QString text = QString::fromStdString(oss.str());

	QLabel *label = new QLabel(text);
	label->setFont(QFont("monospace"));
	label->setContentsMargins(6, 4, 6, 4);

	setWidget(label);
}
