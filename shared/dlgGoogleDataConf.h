#ifndef __DLG_GOOGLE_DATA__
#define __DLG_GOOGLE_DATA__

#include <QDialog>
#include "ui_GoogleData.h"

class dlgGoogleDataConf : public QDialog, public Ui::dlgGoogleDataConf
{

Q_OBJECT
public:
	dlgGoogleDataConf(QWidget *parent = 0);

private slots:
        void accountChanged(const QString& text);
	/* add some slots here */

};

#endif
