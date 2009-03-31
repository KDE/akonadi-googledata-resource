/*  Copyright (C) 2009  Adenilson Cavalcanti <savagobr@yahoo.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; by version 2 of the License or (at your
 *  choice) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifndef __GOOGLE_DATA__
#define __GOOGLE_DATA__

#include <kwallet.h>
#include "dlgGoogleDataConf.h"
class QString;
typedef long unsigned int WId;

extern "C" {
#include <gcalendar.h>
#include <gcontact.h>
}

class GoogleData {

public:
	GoogleData();
	virtual ~GoogleData();

protected:

	int saveToWallet(const QString &user, const QString &pass,
			 const WId &window,
			 const QString &folder = "akonadigoogle",
			 const QString &awallet = "kdewallet");


	int retrieveFromWallet(QString &user, QString &pass,
			       const WId &window,
			       const QString &folder = "akonadigoogle",
			       const QString &awallet = "kdewallet");

	int authenticate(const QString &user, const QString &password);

// 	void retrieveTimestamp(QString &timestamp);
// 	void saveTimestamp(QString &timestamp);

	/* KWallet object pointer */
	KWallet::Wallet *wallet;

	/* Config dialog */
	dlgGoogleDataConf *dlgConf;

	/* Flag with authentication */
	bool authenticated;

	/* Google data context: holds user account name/password */
	gcal_t gcal;

};

#endif
