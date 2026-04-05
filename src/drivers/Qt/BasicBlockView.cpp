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
			std::cerr << "Process " << std::hex << addr << "\n";
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

std::ostream &operator<<(std::ostream &o, const BasicBlockSet &bbSet)
{
	int limit = 4;
	for (auto [addr, bb] : bbSet.basicBlocks)
	{
		if (limit <= 0)
			break;
		o << bb << "\n";
		limit -= 1;
	}
	return o;
}

static BasicBlockView_t *basicBlockViewWin = NULL;
//----------------------------------------------------------------------------
void openBasicBlockViewWindow(QWidget *parent, int force)
{
	if (!force)
	{
		if (basicBlockViewWin != NULL)
			return;
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

	mainLayout->addWidget(new BasicBlockDisplay(bbs));

	setLayout(mainLayout);

	basicBlockViewWin = this;

	restoreGeometry(settings.value("basicBlockView/geometry").toByteArray());

	std::cout << bbs << std::endl;
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
void computeLayers(
	const std::map<uint16, BasicBlockItem *> &addrToBasicBlockItem,
	std::map<uint16, unsigned> &layoutLayer, uint16 startAddr,
	unsigned layer = 0)
{
	if (layoutLayer.count(startAddr) != 0)
	{
		return;
	}
	layoutLayer.insert({startAddr, layer});
	for (auto nextAddr : addrToBasicBlockItem.at(startAddr)->bb_.next())
	{
		computeLayers(addrToBasicBlockItem, layoutLayer, nextAddr, layer + 1);
	}
}

BasicBlockDisplay::BasicBlockDisplay(const BasicBlockSet &bbs, QWidget *parent)
	: QGraphicsView(parent)
{
	setScene(&scene_);
	setDragMode(QGraphicsView::ScrollHandDrag);

	std::map<uint16, BasicBlockItem *> addrToBasicBlockItem;
	std::map<uint16, unsigned> layoutLayer;
	std::map<uint16, unsigned> layoutOffset;
	std::map<uint16, unsigned> layerHeights;
	// void computeLayers(uint16 startAddr, unsigned layer = 0);

	// Insert
	for (const auto &[addr, bb] : bbs.basicBlocks)
	{
	    BasicBlockItem *item = new BasicBlockItem(bb);
		addrToBasicBlockItem.emplace(addr, item);
		scene_.addItem(item);
	}

	// Pick layers
	computeLayers(addrToBasicBlockItem, layoutLayer, bbs.entry);

	// Decide layer heights
	for (const auto &[addr, bbItem] : addrToBasicBlockItem)
	{
		const unsigned layer = layoutLayer[addr];
		const unsigned height = bbItem->size().height();
		if (layerHeights.count(layer) == 0)
		{
			layerHeights.insert({layer, height});
		}
		layerHeights[layer] = std::max(layerHeights[layer], height);
	}

	// Group blocks by layer
	std::map<unsigned, std::vector<uint16>> layers;
	for (const auto &[addr, layer] : layoutLayer)
	{
		layers[layer].push_back(addr);
	}

	// Place vertically
	const unsigned LAYER_GAP = 20;
	for (auto &[addr, bbItem] : addrToBasicBlockItem)
	{
		const unsigned layer = layoutLayer[addr];
		unsigned yPos = 0;
		for (unsigned i = 0; i < layer; ++i)
		{
			yPos += layerHeights[i] + LAYER_GAP;
		}
		bbItem->setY(yPos);
	}

	// Place horizontally: space out blocks within each layer
	const unsigned BLOCK_GAP = 20;
	for (auto &[layer, layerAddrs] : layers)
	{
		qreal x = 0;
		for (uint16 addr : layerAddrs)
		{
			auto *item = addrToBasicBlockItem[addr];
			item->setX(x);
			x += item->size().width() + BLOCK_GAP;
		}
	}

	// Connect
	for (const auto [_, fromBBItem] : addrToBasicBlockItem)
	{
		for (const uint16 toAddr : fromBBItem->bb_.next())
		{
			const BasicBlockItem *toBBItem = addrToBasicBlockItem.at(toAddr);

			QPointF fromPt =
				fromBBItem->pos() + QPointF(fromBBItem->size().width() / 2,
											fromBBItem->size().height());
			QPointF toPt =
				toBBItem->pos() + QPointF(toBBItem->size().width() / 2, 0);

			QPainterPath path(fromPt);
			if (fromBBItem == toBBItem)
			{
				// Self-loop: curve out to the right and back
				qreal w = fromBBItem->size().width();
				qreal h = fromBBItem->size().height();
				QPointF rightEdge = fromBBItem->pos() + QPointF(w + 40, h / 2);
				path.cubicTo(rightEdge + QPointF(0, h / 2),
							 rightEdge + QPointF(0, -h / 2), toPt);
			}
			else
			{
				path.lineTo(toPt);
			}

			auto *pathItem = scene_.addPath(path, QPen(Qt::black, 2));
			pathItem->setZValue(10);
		}
	}
}
//----------------------------------------------------------------------------
BasicBlockItem::BasicBlockItem(const BasicBlock &bb, QGraphicsItem *parent)
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
