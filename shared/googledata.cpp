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

#include "googledata.h"
#include <qstring.h>
#include <qmap.h>
#include <kprotocolmanager.h>

using KWallet::Wallet;

GoogleData::GoogleData(): wallet(0), dlgConf(0), authenticated(false),
			  gcal(NULL)
{
	if (!dlgConf)
		dlgConf = new dlgGoogleDataConf;
}

GoogleData::~GoogleData()
{
	if (dlgConf)
		delete dlgConf;
	if (gcal)
		gcal_delete(gcal);
}

int GoogleData::saveToWallet(const QString &user, const QString &pass,
			     const WId &window, const QString &folder,
			     const QString &awallet)
{
	int result = -1;
	QString gaccount("googleAccount");
	if (wallet == 0)
		wallet = Wallet::openWallet(awallet, window);

	if (wallet == 0)
		return result;

	if (wallet->isOpen()) {
		if (!wallet->hasFolder(folder))
			wallet->createFolder(folder);
		wallet->setFolder(folder);
		QMap<QString, QString> data;
		data["login"] = user;
		data["password"] = pass;
		wallet->writeMap(gaccount, data);
		wallet->sync();
		result = 0;
	}

	return result;
}

int GoogleData::retrieveFromWallet(QString &user,
				   QString &pass,
				   const WId &window,
				   const QString &folder,
				   const QString &awallet)
{
	int result = -1;
	QString gaccount("googleAccount");
	if (wallet == 0)
		wallet = Wallet::openWallet(awallet, window);

	if (wallet == 0)
		return result;

	if (wallet->isOpen()) {
		if (!wallet->hasFolder(folder))
			return result;
		wallet->setFolder(folder);
		QMap<QString, QString> data;
		if (!wallet->readMap(gaccount, data)) {
			user = data["login"];
			pass = data["password"];
			result = 0;
		}
	}

	return result;

}

// void GoogleData::retrieveTimestamp(QString &timestamp)
// {
// 	timestamp = Settings::self()->timestamp();
// }

// void GoogleData::saveTimestamp(QString &timestamp)
// {
//  	Settings::self()->setTimestamp(timestamp);
//  	Settings::self()->writeConfig();
// }

int GoogleData::authenticate(const QString &user,
			     const QString &password)
{

	QByteArray byteUser, bytePass, proxyUrl;
	QString proxy;
	char *l_user, *l_pass, *l_proxy;
	int result = -1;

	byteUser = user.toLocal8Bit();
	bytePass = password.toLocal8Bit();

	l_user = const_cast<char *>(byteUser.constData());
	l_pass = const_cast<char *>(bytePass.constData());

	if (KProtocolManager::useProxy()) {
		proxy = "http";
		proxy = KProtocolManager::proxyFor(proxy);
		if (proxy.length()) {
			proxyUrl = proxy.toLocal8Bit();
			l_proxy = const_cast<char *>(proxyUrl.constData());
			gcal_set_proxy(gcal, l_proxy);
		}
	}

	if (l_user && l_pass)
		if (!(result = gcal_get_authentication(gcal, l_user, l_pass)))
			authenticated = true;

	return result;

}

