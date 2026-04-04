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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <QActionGroup>
#include <QColorDialog>
#include <QFileDialog>
#include <QFontDialog>
#include <QHeaderView>
#include <QInputDialog>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QSpinBox>
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
			// No statically-known successors. We need to walk the whole program
			// to find callers
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
	QSettings settings;
	QVBoxLayout *mainLayout;
	QLabel *lbl;

	setWindowTitle("Basic Block View");

	resize(512, 512);

	mainLayout = new QVBoxLayout();

	lbl = new QLabel(tr("Basic Block View — coming soon"));
	lbl->setAlignment(Qt::AlignCenter);
	mainLayout->addWidget(lbl);

	setLayout(mainLayout);

	basicBlockViewWin = this;

	restoreGeometry(settings.value("basicBlockView/geometry").toByteArray());

	/* Load data */
	uint16 reset, irq, nmi;
	FCEUI_GetIVectors(&reset, &irq, &nmi);

	const BasicBlock reset_bb = BasicBlock::fromAddress(reset);
	std::cout << reset_bb << std::endl;
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
