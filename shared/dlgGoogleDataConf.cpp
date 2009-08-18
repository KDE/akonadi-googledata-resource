/***********************************************************************/
/* googledata.h 						       */
/* 								       */
/* Copyright (C) 2009  Adenilson Cavalcanti <savagobr@yahoo.com>       */
/* 								       */
/* This library is free software; you can redistribute it and/or       */
/* modify it under the terms of the GNU Lesser General Public	       */
/* License as published by the Free Software Foundation; either	       */
/* version 2.1 of the License, or (at your option) any later version.  */
/*   								       */
/* This library is distributed in the hope that it will be useful,     */
/* but WITHOUT ANY WARRANTY; without even the implied warranty of      */
/* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU   */
/* Lesser General Public License for more details.		       */
/*  								       */
/* You should have received a copy of the GNU Lesser General Public    */
/* License along with this library; if not, write to the Free Software */
/* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA       */
/* 02110-1301  USA						       */
/***********************************************************************/

#include "dlgGoogleDataConf.h"
#include <QPushButton>

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
