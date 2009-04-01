#include <QtGui>
#include "dlgGoogleDataConf.h"

dlgGoogleDataConf::dlgGoogleDataConf(QWidget *parent): QDialog(parent)
{
	setupUi(this);

	connect(buttonBox, SIGNAL(accepted()), this, SLOT(accept()));
	connect(buttonBox, SIGNAL(rejected()), this, SLOT(reject()));
}
