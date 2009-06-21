#include "dlgGoogleDataConf.h"
#include <QPushButton>
#include <QLineEdit>

dlgGoogleDataConf::dlgGoogleDataConf(QWidget *parent): QDialog(parent)
{
	setupUi(this);

	connect(buttonBox, SIGNAL(accepted()), this, SLOT(accept()));
	connect(buttonBox, SIGNAL(rejected()), this, SLOT(reject()));
        connect(eAccount, SIGNAL(textChanged ( const QString &)),this,SLOT(accountChanged(const QString&)));
        accountChanged(eAccount->text());
}

void dlgGoogleDataConf::accountChanged(const QString& text)
{
        buttonBox->button(QDialogButtonBox::Ok)->setEnabled(!text.isEmpty()); 
}
