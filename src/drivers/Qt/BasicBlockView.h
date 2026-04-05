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
#include <map>
#include <optional>
#include <deque>
#include <vector>
#include <types.h>

class GraphView;

class BasicBlockView_t : public QDialog
{
Q_OBJECT

public:
	BasicBlockView_t(QWidget *parent = 0);
	~BasicBlockView_t(void);

protected:
	void closeEvent(QCloseEvent *event);

private:
	GraphView *graphView_;

public slots:
	void closeWindow(void);
};

void openBasicBlockViewWindow(QWidget *parent, int force = 0);

//-------- Internal widgets ----------
// These are only here to be picked up by Qt, you should not use these

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
	// GraphView even while mouse is over a block
	void wheelEvent(QGraphicsSceneWheelEvent *event) override
	{
		event->ignore();
	}
};

class GraphView : public QGraphicsView
{
Q_OBJECT

public:
	GraphView(const BasicBlockSet &bbSet, QWidget *parent = nullptr);

private:
	struct Node
	{
		const BasicBlock *bb;
		BasicBlockItem *widget;
		std::vector<Node *> prevs;
		std::vector<Node *> nexts;
		std::optional<double> posX = std::nullopt;
		std::optional<double> posY = std::nullopt;
		std::optional<unsigned> layer = std::nullopt;
	};

	QGraphicsScene scene_;
	std::deque<Node> nodes; // We use a deque, not a vector, because it keeps addresses stable
	std::map<uint16, Node *> addrToNode;
	std::map<uint16, unsigned> layerHeights;

	void computeLayers(Node &node, unsigned layer = 0);
};
