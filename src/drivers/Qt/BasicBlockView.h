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
#include <deque>
#include <map>
#include <optional>
#include <qgraphicsitem.h>
#include <types.h>
#include <vector>

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

class GraphView : public QGraphicsView
{
Q_OBJECT

public:
	GraphView(const BasicBlockSet &bbSet, QWidget *parent = nullptr);

private:
	struct Node
	{
		std::vector<Node *> nexts;
		std::optional<unsigned> layer = std::nullopt;

		virtual QGraphicsItem &asQGraphicsItem() = 0;
		virtual const QGraphicsItem &asQGraphicsItem() const = 0;
		virtual QPointF pos() const = 0;
		virtual QSizeF size() const = 0;
		virtual void setX(qreal x) = 0;
		virtual void setY(qreal y) = 0;
		virtual ~Node() = default;
	};

	class BasicBlockNode : public QGraphicsProxyWidget, public Node
	{
	public:
		BasicBlockNode(const BasicBlock &bb, QGraphicsItem *parent = nullptr);
		const BasicBlock &bb_;
		QGraphicsItem &asQGraphicsItem() override { return *this; }
		const QGraphicsItem &asQGraphicsItem() const override { return *this; }
		QPointF pos() const override { return QGraphicsProxyWidget::pos(); }
		QSizeF size() const override { return QGraphicsProxyWidget::size(); }
		void setX(qreal x) override { QGraphicsProxyWidget::setX(x); }
		void setY(qreal y) override { QGraphicsProxyWidget::setY(y); }

	protected:
		// Ignore wheel events. We want to scroll the
		// GraphView even while mouse is over a block
		void wheelEvent(QGraphicsSceneWheelEvent *event) override
		{
			event->ignore();
		}
	};

	struct DummyNode : public QGraphicsRectItem, public Node
	{
		DummyNode(QPointF pos) : QGraphicsRectItem(pos.x(), pos.y(), 10, 10) {};
		QGraphicsItem &asQGraphicsItem() override { return *this; }
		const QGraphicsItem &asQGraphicsItem() const override { return *this; }
		QPointF pos() const override { return QGraphicsRectItem::pos(); }
		QSizeF size() const override { return boundingRect().size(); }
		void setX(qreal x) override { QGraphicsRectItem::setX(x); }
		void setY(qreal y) override { QGraphicsRectItem::setY(y); }
	};

	QGraphicsScene scene_;
	std::vector<Node*> nodes;
	std::map<uint16, Node *> addrToNode;
	std::map<uint16, unsigned> layerHeights;

	void computeLayers(Node &node, unsigned layer = 0);
};
