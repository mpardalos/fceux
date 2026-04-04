// BasicBlockView.h
//

#pragma once

#include <QWidget>
#include <QDialog>
#include <QVBoxLayout>
#include <QLabel>

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


};

void openBasicBlockViewWindow( QWidget *parent, int force = 0 );
