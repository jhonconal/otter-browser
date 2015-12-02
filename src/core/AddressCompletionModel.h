/**************************************************************************
* Otter Browser: Web browser controlled by the user, not vice-versa.
* Copyright (C) 2013 - 2015 Michal Dutkiewicz aka Emdek <michal@emdek.pl>
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*
**************************************************************************/

#ifndef OTTER_ADDRESSCOMPLETIONMODEL_H
#define OTTER_ADDRESSCOMPLETIONMODEL_H

#include "../core/SearchEnginesManager.h"

#include <QtCore/QAbstractListModel>
#include <QtCore/QUrl>

namespace Otter
{

class AddressCompletionModel : public QAbstractListModel
{
	Q_OBJECT

public:
	enum CompletionType
	{
		UnknownCompletionType = 0,
		BookmarksCompletionType = 1,
		HistoryCompletionType = 2,
		SearchSuggestionsCompletionType = 4,
		SpecialPageCompletionType = 4,
	};

	Q_DECLARE_FLAGS(CompletionTypes, CompletionType)

	enum EntryType
	{
		UnknownType = 0,
		BookmarkType,
		HistoryType,
		TypedInHistoryType,
		SearchSuggestionType,
		SpecialPageType,
	};

	enum EntryRole
	{
		TextRole = Qt::DisplayRole,
		UrlRole = Qt::UserRole,
		TitleRole = (Qt::UserRole + 1),
		MatchRole = (Qt::UserRole + 2),
		KeywordRole = (Qt::UserRole + 3),
		TypeRole = (Qt::UserRole + 4)
	};

	struct CompletionEntry
	{
		QString text;
		QString title;
		QString match;
		QString keyword;
		QUrl url;
		QIcon icon;
		EntryType type;

		CompletionEntry(const QUrl &urlValue, const QString &titleValue, const QString &matchValue, const QIcon &iconValue, EntryType typeValue) : title(titleValue), match(matchValue), url(urlValue), icon(iconValue), type(typeValue) {}
	};

	explicit AddressCompletionModel(QObject *parent = NULL);

	QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const;
	QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const;
	int rowCount(const QModelIndex &index = QModelIndex()) const;

public slots:
	void setFilter(const QString &filter = QString());

protected:
	void timerEvent(QTimerEvent *event);

private:
	QList<CompletionEntry> m_completions;
	QString m_filter;
	SearchEnginesManager::SearchEngineDefinition m_defaultSearchEngine;
	AddressCompletionModel::CompletionTypes m_types;
	int m_updateTimer;

signals:
	void completionReady(const QString &filter);
};

}

#endif
