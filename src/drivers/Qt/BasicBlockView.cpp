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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */
//
// BasicBlockView.cpp
//
#include <QSettings>
#include <QCloseEvent>

#include "Qt/BasicBlockView.h"

static BasicBlockView_t *basicBlockViewWin = NULL;
//----------------------------------------------------------------------------
void openBasicBlockViewWindow( QWidget *parent, int force )
{
   if ( !force )
   {
      if ( basicBlockViewWin != NULL ) return;
   }
   basicBlockViewWin = new BasicBlockView_t(parent);

   basicBlockViewWin->show();
}
//----------------------------------------------------------------------------
BasicBlockView_t::BasicBlockView_t(QWidget *parent)
	: QDialog( parent, Qt::Window )
{
	QSettings settings;
	QVBoxLayout *mainLayout;
	QLabel *lbl;

	setWindowTitle("Basic Block View");

	resize( 512, 512 );

	mainLayout = new QVBoxLayout();

	lbl = new QLabel( tr("Basic Block View — coming soon") );
	lbl->setAlignment( Qt::AlignCenter );
	mainLayout->addWidget( lbl );

	setLayout( mainLayout );

	basicBlockViewWin = this;

	restoreGeometry(settings.value("basicBlockView/geometry").toByteArray());
}
//----------------------------------------------------------------------------
BasicBlockView_t::~BasicBlockView_t(void)
{
	QSettings settings;

	if ( basicBlockViewWin == this )
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
