// BasicBlockView.h
//

#pragma once

#include <QDialog>
#include <QGraphicsProxyWidget>
#include <QGraphicsScene>
#include <QGraphicsSceneWheelEvent>
#include <QGraphicsView>
#include <QLabel>
#include <QVBoxLayout>
#include <QWidget>
#include <types.h>

class BasicBlockView_t : public QDialog
{
Q_OBJECT

public:
	BasicBlockView_t(QWidget *parent = 0);
	~BasicBlockView_t(void);

protected:
	void closeEvent(QCloseEvent *event);

public slots:
	void closeWindow(void);
};

void openBasicBlockViewWindow(QWidget *parent, int force = 0);

//-------- Internal widgets ----------

struct BasicBlock;
struct BasicBlockSet;

class BasicBlockItem : public QGraphicsProxyWidget
{
Q_OBJECT

public:
	BasicBlockItem(const BasicBlock &bb, QGraphicsItem *parent = nullptr);
	const BasicBlock &bb_;

protected:
	// Ignore wheel events. We want to scroll the
	// BasicBlockDisplay even while mouse is over a block
	void wheelEvent(QGraphicsSceneWheelEvent *event) override
	{
		event->ignore();
	}
};

class BasicBlockDisplay : public QGraphicsView
{
Q_OBJECT

public:
	BasicBlockDisplay(const BasicBlockSet &bbs, QWidget *parent = nullptr);

private:
	QGraphicsScene scene_;
	std::map<uint16, const BasicBlockItem *> addrToBasicBlockItem;
};
