/**************************************************************************
* Otter Browser: Web browser controlled by the user, not vice-versa.
* Copyright (C) 2013 - 2017 Michal Dutkiewicz aka Emdek <michal@emdek.pl>
* Copyright (C) 2014 Piotr Wójcik <chocimier@tlen.pl>
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program. If not, see <http://www.gnu.org/licenses/>.
*
**************************************************************************/

#include "WindowsManager.h"
#include "ActionsManager.h"
#include "Application.h"
#include "BookmarksManager.h"
#include "SettingsManager.h"
#include "Utils.h"
#include "WebBackend.h"
#include "../ui/ContentsWidget.h"
#include "../ui/MainWindow.h"
#include "../ui/TabBarWidget.h"
#include "../ui/Window.h"
#include "../ui/WorkspaceWidget.h"

#include <QtGui/QStatusTipEvent>
#include <QtWidgets/QAction>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QMessageBox>

namespace Otter
{

WindowsManager::WindowsManager(bool isPrivate, MainWindow *parent) : QObject(parent),
	m_mainWindow(parent),
	m_isPrivate(isPrivate),
	m_isRestored(false)
{
}

void WindowsManager::triggerAction(int identifier, const QVariantMap &parameters)
{
	Window *window(nullptr);

	if (parameters.contains(QLatin1String("window")))
	{
		window = getWindowByIdentifier(parameters[QLatin1String("window")].toULongLong());
	}
	else
	{
		window = m_mainWindow->getWorkspace()->getActiveWindow();
	}

	switch (identifier)
	{
		case ActionsManager::NewTabAction:
			{
				QVariantMap mutableParameters(parameters);
				mutableParameters[QLatin1String("hints")] = NewTabOpen;

				triggerAction(ActionsManager::OpenUrlAction, mutableParameters);
			}

			break;
		case ActionsManager::NewTabPrivateAction:
			{
				QVariantMap mutableParameters(parameters);
				mutableParameters[QLatin1String("hints")] = QVariant(NewTabOpen | PrivateOpen);

				triggerAction(ActionsManager::OpenUrlAction, mutableParameters);
			}

			break;
		case ActionsManager::CloneTabAction:
			if (window && window->canClone())
			{
				addWindow(window->clone(true, m_mainWindow->getWorkspace()));
			}

			break;
		case ActionsManager::PinTabAction:
			if (window)
			{
				window->setPinned(!window->isPinned());
			}

			break;
		case ActionsManager::DetachTabAction:
			if (window && m_mainWindow->getTabBar()->count() > 1)
			{
				moveWindow(window);
			}

			break;
		case ActionsManager::CloseTabAction:
			if (window)
			{
				close(getWindowIndex(window->getIdentifier()));
			}

			break;
		case ActionsManager::CloseOtherTabsAction:
			if (window)
			{
				const int index(getWindowIndex(window->getIdentifier()));

				if (index >= 0)
				{
					closeOther(index);
				}
			}

			break;
		case ActionsManager::ClosePrivateTabsAction:
			m_mainWindow->getAction(ActionsManager::ClosePrivateTabsAction)->setEnabled(false);

			for (int i = (m_mainWindow->getTabBar()->count() - 1); i > 0; --i)
			{
				if (getWindowByIndex(i)->isPrivate())
				{
					close(i);
				}
			}

			break;
		case ActionsManager::OpenUrlAction:
			{
				const QUrl url((parameters[QLatin1String("url")].type() == QVariant::Url) ? parameters[QLatin1String("url")].toUrl() : QUrl::fromUserInput(parameters[QLatin1String("url")].toString()));

				if (parameters.contains(QLatin1String("application")))
				{
					Utils::runApplication(parameters[QLatin1String("application")].toString(), url);

					break;
				}

				OpenHints hints(calculateOpenHints(parameters));
				int index(parameters.value(QLatin1String("index"), -1).toInt());

				if (m_isPrivate)
				{
					hints |= PrivateOpen;
				}

				QVariantMap mutableParameters(parameters);
				mutableParameters[QLatin1String("hints")] = QVariant(hints);

				if (hints.testFlag(NewWindowOpen))
				{
					Application::createWindow(mutableParameters);

					break;
				}

				if (index >= 0)
				{
					Window *window(new Window(mutableParameters));

					addWindow(window, hints, index);

					window->setUrl(((url.isEmpty() && SettingsManager::getOption(SettingsManager::StartPage_EnableStartPageOption).toBool()) ? QUrl(QLatin1String("about:start")) : url), false);

					break;
				}

				Window *activeWindow(m_mainWindow->getWorkspace()->getActiveWindow());
				const bool isUrlEmpty(activeWindow && activeWindow->getLoadingState() == WindowsManager::FinishedLoadingState && Utils::isUrlEmpty(activeWindow->getUrl()));

				if (hints == NewTabOpen && !url.isEmpty() && isUrlEmpty)
				{
					hints = CurrentTabOpen;
				}
				else if (hints == DefaultOpen && url.scheme() == QLatin1String("about") && !url.path().isEmpty() && url.path() != QLatin1String("blank") && url.path() != QLatin1String("start") && (!activeWindow || !Utils::isUrlEmpty(activeWindow->getUrl())))
				{
					hints = NewTabOpen;
				}
				else if (hints == DefaultOpen && url.scheme() != QLatin1String("javascript") && (isUrlEmpty || SettingsManager::getOption(SettingsManager::Browser_ReuseCurrentTabOption).toBool()))
				{
					hints = CurrentTabOpen;
				}

				mutableParameters[QLatin1String("hints")] = QVariant(hints);

				if (hints.testFlag(CurrentTabOpen))
				{
					if (activeWindow && activeWindow->getType() == QLatin1String("web") && activeWindow->getWebWidget() && !parameters.contains(QLatin1String("webBackend")))
					{
						mutableParameters[QLatin1String("webBackend")] = activeWindow->getWebWidget()->getBackend()->getName();
					}

					close(m_mainWindow->getTabBar()->currentIndex());
				}

				Window *window(new Window(mutableParameters));

				addWindow(window, hints, index);

				window->setUrl(((url.isEmpty() && SettingsManager::getOption(SettingsManager::StartPage_EnableStartPageOption).toBool()) ? QUrl(QLatin1String("about:start")) : url), false);
			}

			break;
		case ActionsManager::ReopenTabAction:
			restore();

			break;
		case ActionsManager::StopAllAction:
			for (int i = 0; i < m_mainWindow->getTabBar()->count(); ++i)
			{
				getWindowByIndex(i)->triggerAction(ActionsManager::StopAction);
			}

			break;
		case ActionsManager::ReloadAllAction:
			for (int i = 0; i < m_mainWindow->getTabBar()->count(); ++i)
			{
				getWindowByIndex(i)->triggerAction(ActionsManager::ReloadAction);
			}

			break;
		case ActionsManager::ActivatePreviouslyUsedTabAction:
		case ActionsManager::ActivateLeastRecentlyUsedTabAction:
			{
				QMultiMap<qint64, quint64> map;
				const bool includeMinimized(parameters.contains(QLatin1String("includeMinimized")));

				for (int i = 0; i < getWindowCount(); ++i)
				{
					Window *window(getWindowByIndex(i));

					if (window && (includeMinimized || !window->isMinimized()))
					{
						map.insert(window->getLastActivity().toMSecsSinceEpoch(), window->getIdentifier());
					}
				}

				const QList<quint64> list(map.values());

				if (list.count() > 1)
				{
					setActiveWindowByIdentifier((identifier == ActionsManager::ActivatePreviouslyUsedTabAction) ? list.at(list.count() - 2) : list.first());
				}
			}

			break;
		case ActionsManager::ActivateTabAction:
			if (window)
			{
				setActiveWindowByIdentifier(window->getIdentifier());
			}

			break;
		case ActionsManager::ActivateTabOnLeftAction:
			m_mainWindow->getTabBar()->activateTabOnLeft();

			break;
		case ActionsManager::ActivateTabOnRightAction:
			m_mainWindow->getTabBar()->activateTabOnRight();

			break;
		case ActionsManager::BookmarkAllOpenPagesAction:
			for (int i = 0; i < m_mainWindow->getTabBar()->count(); ++i)
			{
				Window *window(getWindowByIndex(i));

				if (window && !Utils::isUrlEmpty(window->getUrl()))
				{
					BookmarksManager::addBookmark(BookmarksModel::UrlBookmark, window->getUrl(), window->getTitle());
				}
			}

			break;
		case ActionsManager::OpenBookmarkAction:
			if (parameters.contains(QLatin1String("bookmark")))
			{
				BookmarksItem *bookmark(BookmarksManager::getBookmark(parameters[QLatin1String("bookmark")].toULongLong()));

				if (!bookmark)
				{
					break;
				}

				QVariantMap mutableParameters(parameters);
				mutableParameters.remove(QLatin1String("bookmark"));

				switch (static_cast<BookmarksModel::BookmarkType>(bookmark->data(BookmarksModel::TypeRole).toInt()))
				{
					case BookmarksModel::UrlBookmark:
						mutableParameters[QLatin1String("url")] = bookmark->data(BookmarksModel::UrlRole).toUrl();

						triggerAction(ActionsManager::OpenUrlAction, mutableParameters);

						break;
					case BookmarksModel::RootBookmark:
					case BookmarksModel::FolderBookmark:
						{
							const QVector<QUrl> urls(bookmark->getUrls());
							bool canOpen(true);

							if (urls.count() > 1 && SettingsManager::getOption(SettingsManager::Choices_WarnOpenBookmarkFolderOption).toBool() && !parameters.value(QLatin1String("ignoreWarning"), false).toBool())
							{
								QMessageBox messageBox;
								messageBox.setWindowTitle(tr("Question"));
								messageBox.setText(tr("You are about to open %n bookmark(s).", "", urls.count()));
								messageBox.setInformativeText(tr("Do you want to continue?"));
								messageBox.setIcon(QMessageBox::Question);
								messageBox.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
								messageBox.setDefaultButton(QMessageBox::Yes);
								messageBox.setCheckBox(new QCheckBox(tr("Do not show this message again")));

								if (messageBox.exec() == QMessageBox::Cancel)
								{
									canOpen = false;
								}

								SettingsManager::setValue(SettingsManager::Choices_WarnOpenBookmarkFolderOption, !messageBox.checkBox()->isChecked());
							}

							if (urls.isEmpty() || !canOpen)
							{
								break;
							}

							const OpenHints hints(calculateOpenHints(parameters));
							int index(parameters.value(QLatin1String("index"), -1).toInt());

							if (index < 0)
							{
								index = ((!hints.testFlag(EndOpen) && SettingsManager::getOption(SettingsManager::TabBar_OpenNextToActiveOption).toBool()) ? (m_mainWindow->getTabBar()->currentIndex() + 1) : m_mainWindow->getTabBar()->count());
							}

							mutableParameters[QLatin1String("url")] = urls.at(0);
							mutableParameters[QLatin1String("hints")] = QVariant(hints);
							mutableParameters[QLatin1String("index")] = index;

							triggerAction(ActionsManager::OpenUrlAction, mutableParameters);

							mutableParameters[QLatin1String("hints")] = QVariant((hints == DefaultOpen || hints.testFlag(CurrentTabOpen)) ? NewTabOpen : hints);

							for (int i = 1; i < urls.count(); ++i)
							{
								mutableParameters[QLatin1String("url")] = urls.at(i);
								mutableParameters[QLatin1String("index")] = (index + i);

								triggerAction(ActionsManager::OpenUrlAction, mutableParameters);
							}
						}

						break;
					default:
						break;
				}
			}

			break;
		default:
			if (identifier == ActionsManager::PasteAndGoAction && (!window || window->getType() != QLatin1String("web")))
			{
				QVariantMap mutableParameters(parameters);

				if (m_isPrivate)
				{
					mutableParameters[QLatin1String("hints")] = QVariant(calculateOpenHints(parameters) | PrivateOpen);
				}

				window = new Window(mutableParameters);

				addWindow(window, NewTabOpen);

				window->triggerAction(ActionsManager::PasteAndGoAction);
			}
			else if (window)
			{
				window->triggerAction(identifier, parameters);
			}

			break;
	}
}

void WindowsManager::open(const QUrl &url, OpenHints hints)
{
	triggerAction(ActionsManager::OpenUrlAction, {{QLatin1String("url"), url}, {QLatin1String("hints"), QVariant(hints)}});

}

void WindowsManager::open(BookmarksItem *bookmark, OpenHints hints)
{
	if (bookmark)
	{
		triggerAction(ActionsManager::OpenBookmarkAction, {{QLatin1String("bookmark"), bookmark->data(BookmarksModel::IdentifierRole)}, {QLatin1String("hints"), QVariant(hints)}});
	}
}

void WindowsManager::search(const QString &query, const QString &searchEngine, OpenHints hints)
{
	Window *window(m_mainWindow->getWorkspace()->getActiveWindow());
	const bool isUrlEmpty(window && window->getLoadingState() == WindowsManager::FinishedLoadingState && Utils::isUrlEmpty(window->getUrl()));

	if ((hints == NewTabOpen && isUrlEmpty) || (hints == DefaultOpen && (isUrlEmpty || SettingsManager::getOption(SettingsManager::Browser_ReuseCurrentTabOption).toBool())))
	{
		hints = CurrentTabOpen;
	}

	if (window && hints.testFlag(CurrentTabOpen) && window->getType() == QLatin1String("web"))
	{
		window->search(query, searchEngine);

		return;
	}

	if (window && window->canClone())
	{
		window = window->clone(false, m_mainWindow->getWorkspace());

		addWindow(window, hints);
	}
	else
	{
		triggerAction(ActionsManager::OpenUrlAction, {{QLatin1String("hints"), QVariant(hints)}});

		window = m_mainWindow->getWorkspace()->getActiveWindow();
	}

	if (window)
	{
		window->search(query, searchEngine);
	}
}

void WindowsManager::close(int index)
{
	if (index < 0 || index >= m_mainWindow->getTabBar()->count())
	{
		return;
	}

	Window *window(getWindowByIndex(index));

	if (window && !window->isPinned())
	{
		window->close();
	}
}

void WindowsManager::closeOther(int index)
{
	if (index < 0)
	{
		index = m_mainWindow->getTabBar()->currentIndex();
	}

	if (index < 0 || index >= m_mainWindow->getTabBar()->count())
	{
		return;
	}

	for (int i = (m_mainWindow->getTabBar()->count() - 1); i > index; --i)
	{
		close(i);
	}

	for (int i = (index - 1); i >= 0; --i)
	{
		close(i);
	}
}

void WindowsManager::closeAll()
{
	for (int i = (m_mainWindow->getTabBar()->count() - 1); i >= 0; --i)
	{
		close(i);
	}
}

void WindowsManager::restore(const SessionMainWindow &session)
{
	int index(session.index);

	if (index >= session.windows.count())
	{
		index = -1;
	}

	if (session.windows.isEmpty())
	{
		m_isRestored = true;

		if (SettingsManager::getOption(SettingsManager::Interface_LastTabClosingActionOption).toString() != QLatin1String("doNothing"))
		{
			triggerAction(ActionsManager::NewTabAction);
		}
		else
		{
			m_mainWindow->setCurrentWindow(nullptr);
		}
	}
	else
	{
		QVariantMap parameters;

		if (m_isPrivate)
		{
			parameters[QLatin1String("hints")] = PrivateOpen;
		}

		for (int i = 0; i < session.windows.count(); ++i)
		{
			Window *window(new Window(parameters));
			window->setSession(session.windows.at(i));

			if (index < 0 && session.windows.at(i).state != MinimizedWindowState)
			{
				index = i;
			}

			addWindow(window, DefaultOpen, -1, session.windows.at(i).geometry, session.windows.at(i).state, session.windows.at(i).isAlwaysOnTop);
		}
	}

	m_isRestored = true;

	connect(SessionsManager::getInstance(), SIGNAL(requestedRemoveStoredUrl(QString)), this, SLOT(removeStoredUrl(QString)));
	connect(m_mainWindow->getTabBar(), SIGNAL(currentChanged(int)), this, SLOT(setActiveWindowByIndex(int)));

	setActiveWindowByIndex(index);

	m_mainWindow->getWorkspace()->markRestored();
}

void WindowsManager::restore(int index)
{
	if (index < 0 || index >= m_closedWindows.count())
	{
		return;
	}

	const ClosedWindow closedWindow(m_closedWindows.at(index));
	int windowIndex(-1);

	if (closedWindow.previousWindow == 0)
	{
		windowIndex = 0;
	}
	else if (closedWindow.nextWindow == 0)
	{
		windowIndex = m_mainWindow->getTabBar()->count();
	}
	else
	{
		const int previousIndex(getWindowIndex(closedWindow.previousWindow));

		if (previousIndex >= 0)
		{
			windowIndex = (previousIndex + 1);
		}
		else
		{
			const int nextIndex(getWindowIndex(closedWindow.nextWindow));

			if (nextIndex >= 0)
			{
				windowIndex = qMax(0, (nextIndex - 1));
			}
		}
	}

	QVariantMap parameters;

	if (m_isPrivate || closedWindow.isPrivate)
	{
		parameters[QLatin1String("hints")] = PrivateOpen;
	}

	Window *window(new Window(parameters));
	window->setSession(closedWindow.window);

	m_closedWindows.removeAt(index);

	if (m_closedWindows.isEmpty() && SessionsManager::getClosedWindows().isEmpty())
	{
		emit closedWindowsAvailableChanged(false);
	}

	addWindow(window, DefaultOpen, windowIndex);
}

void WindowsManager::clearClosedWindows()
{
	m_closedWindows.clear();

	if (SessionsManager::getClosedWindows().isEmpty())
	{
		emit closedWindowsAvailableChanged(false);
	}
}

void WindowsManager::addWindow(Window *window, OpenHints hints, int index, const QRect &geometry, WindowState state, bool isAlwaysOnTop)
{
	if (!window)
	{
		return;
	}

	m_windows[window->getIdentifier()] = window;

	if (window->isPrivate())
	{
		m_mainWindow->getAction(ActionsManager::ClosePrivateTabsAction)->setEnabled(true);
	}

	window->setToolBarsVisible(m_mainWindow->areToolBarsVisible());

	if (index < 0)
	{
		index = ((!hints.testFlag(EndOpen) && SettingsManager::getOption(SettingsManager::TabBar_OpenNextToActiveOption).toBool()) ? (m_mainWindow->getTabBar()->currentIndex() + 1) : m_mainWindow->getTabBar()->count());
	}

	if (!window->isPinned())
	{
		const int offset(m_mainWindow->getTabBar()->getPinnedTabsAmount());

		if (index < offset)
		{
			index = offset;
		}
	}

	const QString newTabOpeningAction(SettingsManager::getOption(SettingsManager::Interface_NewTabOpeningActionOption).toString());

	if (m_isRestored && newTabOpeningAction == QLatin1String("maximizeTab"))
	{
		state = MaximizedWindowState;
	}

	m_mainWindow->getTabBar()->addTab(index, window);
	m_mainWindow->getWorkspace()->addWindow(window, geometry, state, isAlwaysOnTop);
	m_mainWindow->getAction(ActionsManager::CloseTabAction)->setEnabled(!window->isPinned());

	if (!hints.testFlag(BackgroundOpen) || m_mainWindow->getTabBar()->count() < 2)
	{
		m_mainWindow->getTabBar()->setCurrentIndex(index);

		if (m_isRestored)
		{
			setActiveWindowByIndex(index);
		}
	}

	if (m_isRestored)
	{
		if (newTabOpeningAction == QLatin1String("cascadeAll"))
		{
			ActionsManager::triggerAction(ActionsManager::CascadeAllAction, this);
		}
		else if (newTabOpeningAction == QLatin1String("tileAll"))
		{
			ActionsManager::triggerAction(ActionsManager::TileAllAction, this);
		}
	}

	connect(m_mainWindow, SIGNAL(areToolBarsVisibleChanged(bool)), window, SLOT(setToolBarsVisible(bool)));
	connect(window, &Window::needsAttention, [&]()
	{
		QApplication::alert(m_mainWindow);
	});
	connect(window, SIGNAL(titleChanged(QString)), this, SLOT(setTitle(QString)));
	connect(window, SIGNAL(requestedOpenUrl(QUrl,WindowsManager::OpenHints)), this, SLOT(open(QUrl,WindowsManager::OpenHints)));
	connect(window, SIGNAL(requestedOpenBookmark(BookmarksItem*,WindowsManager::OpenHints)), this, SLOT(open(BookmarksItem*,WindowsManager::OpenHints)));
	connect(window, SIGNAL(requestedSearch(QString,QString,WindowsManager::OpenHints)), this, SLOT(search(QString,QString,WindowsManager::OpenHints)));
	connect(window, SIGNAL(requestedAddBookmark(QUrl,QString,QString)), this, SIGNAL(requestedAddBookmark(QUrl,QString,QString)));
	connect(window, SIGNAL(requestedEditBookmark(QUrl)), this, SIGNAL(requestedEditBookmark(QUrl)));
	connect(window, SIGNAL(requestedNewWindow(ContentsWidget*,WindowsManager::OpenHints)), this, SLOT(openWindow(ContentsWidget*,WindowsManager::OpenHints)));
	connect(window, SIGNAL(requestedCloseWindow(Window*)), this, SLOT(handleWindowClose(Window*)));
	connect(window, SIGNAL(isPinnedChanged(bool)), this, SLOT(handleWindowIsPinnedChanged(bool)));

	emit windowAdded(window->getIdentifier());
}

void WindowsManager::moveWindow(Window *window, MainWindow *mainWindow, int index)
{
	Window *newWindow(nullptr);
	OpenHints hints(mainWindow ? DefaultOpen : NewWindowOpen);

	if (window->isPrivate())
	{
		hints |= PrivateOpen;
	}

	window->getContentsWidget()->setParent(nullptr);

	if (mainWindow)
	{
		newWindow = mainWindow->getWindowsManager()->openWindow(window->getContentsWidget(), hints, index);
	}
	else
	{
		newWindow = openWindow(window->getContentsWidget(), hints);
	}

	if (newWindow && window->isPinned())
	{
		newWindow->setPinned(true);
	}

	m_mainWindow->getTabBar()->removeTab(getWindowIndex(window->getIdentifier()));

	m_windows.remove(window->getIdentifier());

	if (mainWindow && m_windows.isEmpty())
	{
		m_mainWindow->close();
	}
	else
	{
		Action *closePrivateTabsAction(m_mainWindow->getAction(ActionsManager::ClosePrivateTabsAction));

		if (closePrivateTabsAction->isEnabled() && getWindowCount(true) == 0)
		{
			closePrivateTabsAction->setEnabled(false);
		}

		emit windowRemoved(window->getIdentifier());
	}
}

void WindowsManager::removeStoredUrl(const QString &url)
{
	for (int i = (m_closedWindows.count() - 1); i >= 0; --i)
	{
		if (url == m_closedWindows.at(i).window.getUrl())
		{
			m_closedWindows.removeAt(i);

			break;
		}
	}

	if (m_closedWindows.isEmpty())
	{
		emit closedWindowsAvailableChanged(false);
	}
}

void WindowsManager::handleWindowClose(Window *window)
{
	const int index(window ? getWindowIndex(window->getIdentifier()) : -1);

	if (index < 0)
	{
		return;
	}

	if (window && (!window->isPrivate() || SettingsManager::getOption(SettingsManager::History_RememberClosedPrivateTabsOption).toBool()))
	{
		const WindowHistoryInformation history(window->getContentsWidget()->getHistory());

		if (!Utils::isUrlEmpty(window->getUrl()) || history.entries.count() > 1)
		{
			Window *nextWindow(getWindowByIndex(index + 1));
			Window *previousWindow((index > 0) ? getWindowByIndex(index - 1) : nullptr);

			ClosedWindow closedWindow;
			closedWindow.window = window->getSession();
			closedWindow.nextWindow = (nextWindow ? nextWindow->getIdentifier() : 0);
			closedWindow.previousWindow = (previousWindow ? previousWindow->getIdentifier() : 0);
			closedWindow.isPrivate = window->isPrivate();

			if (window->getType() != QLatin1String("web"))
			{
				removeStoredUrl(closedWindow.window.getUrl());
			}

			m_closedWindows.prepend(closedWindow);

			emit closedWindowsAvailableChanged(true);
		}
	}

	const QString lastTabClosingAction(SettingsManager::getOption(SettingsManager::Interface_LastTabClosingActionOption).toString());

	if (m_mainWindow->getTabBar()->count() == 1)
	{
		if (lastTabClosingAction == QLatin1String("closeWindow") || (lastTabClosingAction == QLatin1String("closeWindowIfNotLast") && Application::getWindows().count() > 1))
		{
			m_mainWindow->triggerAction(ActionsManager::CloseWindowAction);

			return;
		}

		if (lastTabClosingAction == QLatin1String("openTab"))
		{
			window = getWindowByIndex(0);

			if (window)
			{
				window->clear();

				return;
			}
		}
		else
		{
			m_mainWindow->getAction(ActionsManager::CloseTabAction)->setEnabled(false);
			m_mainWindow->setCurrentWindow(nullptr);

			emit titleChanged(QString());
		}
	}

	m_mainWindow->getTabBar()->removeTab(index);

	Action *closePrivateTabsAction(m_mainWindow->getAction(ActionsManager::ClosePrivateTabsAction));

	if (closePrivateTabsAction->isEnabled() && getWindowCount(true) == 0)
	{
		closePrivateTabsAction->setEnabled(false);
	}

	emit windowRemoved(window->getIdentifier());

	m_windows.remove(window->getIdentifier());

	if (m_mainWindow->getTabBar()->count() < 1 && lastTabClosingAction == QLatin1String("openTab"))
	{
		triggerAction(ActionsManager::NewTabAction);
	}
}

void WindowsManager::handleWindowIsPinnedChanged(bool isPinned)
{
	Window *window(qobject_cast<Window*>(sender()));

	if (window && window == m_mainWindow->getWorkspace()->getActiveWindow())
	{
		m_mainWindow->getAction(ActionsManager::CloseTabAction)->setEnabled(!isPinned);
	}
}

void WindowsManager::setOption(int identifier, const QVariant &value)
{
	Window *window(m_mainWindow->getWorkspace()->getActiveWindow());

	if (window)
	{
		window->setOption(identifier, value);
	}
}

void WindowsManager::setZoom(int zoom)
{
	Window *window(m_mainWindow->getWorkspace()->getActiveWindow());

	if (window)
	{
		window->getContentsWidget()->setZoom(zoom);
	}
}

void WindowsManager::setActiveWindowByIndex(int index)
{
	if (!m_isRestored || index >= m_mainWindow->getTabBar()->count())
	{
		return;
	}

	if (index != m_mainWindow->getTabBar()->currentIndex())
	{
		m_mainWindow->getTabBar()->setCurrentIndex(index);

		return;
	}

	Window *window(m_mainWindow->getWorkspace()->getActiveWindow());

	if (window)
	{
		disconnect(window, SIGNAL(statusMessageChanged(QString)), this, SLOT(setStatusMessage(QString)));
		disconnect(window, SIGNAL(zoomChanged(int)), this, SIGNAL(zoomChanged(int)));
		disconnect(window, SIGNAL(canZoomChanged(bool)), this, SIGNAL(canZoomChanged(bool)));
	}

	setStatusMessage(QString());

	window = getWindowByIndex(index);

	m_mainWindow->getAction(ActionsManager::CloseTabAction)->setEnabled(window && !window->isPinned());
	m_mainWindow->setCurrentWindow(window);

	if (window)
	{
		m_mainWindow->getWorkspace()->setActiveWindow(window);

		window->setFocus();
		window->markAsActive();

		setStatusMessage(window->getContentsWidget()->getStatusMessage());

		emit titleChanged(window->getContentsWidget()->getTitle());
		emit zoomChanged(window->getContentsWidget()->getZoom());
		emit canZoomChanged(window->getContentsWidget()->canZoom());

		connect(window, SIGNAL(statusMessageChanged(QString)), this, SLOT(setStatusMessage(QString)));
		connect(window, SIGNAL(zoomChanged(int)), this, SIGNAL(zoomChanged(int)));
		connect(window, SIGNAL(canZoomChanged(bool)), this, SIGNAL(canZoomChanged(bool)));
	}

	m_mainWindow->getAction(ActionsManager::CloneTabAction)->setEnabled(window && window->canClone());

	emit currentWindowChanged(window ? window->getIdentifier() : 0);
}

void WindowsManager::setActiveWindowByIdentifier(quint64 identifier)
{
	for (int i = 0; i < m_mainWindow->getTabBar()->count(); ++i)
	{
		Window *window(getWindowByIndex(i));

		if (window && window->getIdentifier() == identifier)
		{
			setActiveWindowByIndex(i);

			break;
		}
	}
}

void WindowsManager::setTitle(const QString &title)
{
	QString text(title.isEmpty() ? tr("Empty") : title);
	Window *window(qobject_cast<Window*>(sender()));

	if (!window)
	{
		return;
	}

	const int index(getWindowIndex(window->getIdentifier()));

	if (index == m_mainWindow->getTabBar()->currentIndex())
	{
		emit titleChanged(text);
	}
}

void WindowsManager::setStatusMessage(const QString &message)
{
	QStatusTipEvent event(message);

	QApplication::sendEvent(m_mainWindow, &event);
}

Action* WindowsManager::getAction(int identifier)
{
	Window *window(m_mainWindow->getWorkspace()->getActiveWindow());

	return (window ? window->getContentsWidget()->getAction(identifier) : nullptr);
}

Window* WindowsManager::openWindow(ContentsWidget *widget, OpenHints hints, int index)
{
	if (!widget)
	{
		return nullptr;
	}

	Window *window(nullptr);

	if (widget->isPrivate())
	{
		hints |= PrivateOpen;
	}

	if (hints.testFlag(NewWindowOpen))
	{
		MainWindow *mainWindow(Application::createWindow({{QLatin1String("hints"), QVariant(hints)}}));

		if (mainWindow)
		{
			window = mainWindow->getWindowsManager()->openWindow(widget, (hints.testFlag(PrivateOpen) ? PrivateOpen : DefaultOpen));

			mainWindow->getWindowsManager()->closeOther();
		}
	}
	else
	{
		window = new Window({{QLatin1String("hints"), QVariant(hints)}}, widget);

		addWindow(window, hints, index);
	}

	return window;
}

Window* WindowsManager::getWindowByIndex(int index) const
{
	if (index == -1)
	{
		index = m_mainWindow->getTabBar()->currentIndex();
	}

	return m_mainWindow->getTabBar()->getWindow(index);
}

Window* WindowsManager::getWindowByIdentifier(quint64 identifier) const
{
	return (m_windows.contains(identifier) ? m_windows[identifier] : nullptr);
}

QVariant WindowsManager::getOption(int identifier) const
{
	Window *window(m_mainWindow->getWorkspace()->getActiveWindow());

	return (window ? window->getContentsWidget()->getOption(identifier) : QVariant());
}

QString WindowsManager::getTitle() const
{
	Window *window(m_mainWindow->getWorkspace()->getActiveWindow());

	return (window ? window->getTitle() : tr("Empty"));
}

QUrl WindowsManager::getUrl() const
{
	Window *window(m_mainWindow->getWorkspace()->getActiveWindow());

	return (window ? window->getUrl() : QUrl());
}

SessionMainWindow WindowsManager::getSession() const
{
	SessionMainWindow session;
	session.geometry = m_mainWindow->saveGeometry();
	session.index = m_mainWindow->getTabBar()->currentIndex();

	for (int i = 0; i < m_mainWindow->getTabBar()->count(); ++i)
	{
		Window *window(getWindowByIndex(i));

		if (window && !window->isPrivate())
		{
			session.windows.append(window->getSession());
		}
		else if (i < session.index)
		{
			--session.index;
		}
	}

	return session;
}

QVector<ClosedWindow> WindowsManager::getClosedWindows() const
{
	return m_closedWindows;
}

QVector<quint64> WindowsManager::getWindows() const
{
	QVector<quint64> windows;
	windows.reserve(m_mainWindow->getTabBar()->count());

	for (int i = 0; i < m_mainWindow->getTabBar()->count(); ++i)
	{
		Window *window(getWindowByIndex(i));

		if (window)
		{
			windows.append(window->getIdentifier());
		}
	}

	return windows;
}

WindowsManager::OpenHints WindowsManager::calculateOpenHints(OpenHints hints, Qt::MouseButton button, int modifiers)
{
	const bool useNewTab(!hints.testFlag(NewWindowOpen) && SettingsManager::getOption(SettingsManager::Browser_OpenLinksInNewTabOption).toBool());
	const Qt::KeyboardModifiers keyboardModifiers((modifiers == -1) ? QGuiApplication::keyboardModifiers() : static_cast<Qt::KeyboardModifiers>(modifiers));

	if (button == Qt::MiddleButton && keyboardModifiers.testFlag(Qt::AltModifier))
	{
		return ((useNewTab ? NewTabOpen : NewWindowOpen) | BackgroundOpen | EndOpen);
	}

	if (keyboardModifiers.testFlag(Qt::ControlModifier) || button == Qt::MiddleButton)
	{
		return ((useNewTab ? NewTabOpen : NewWindowOpen) | BackgroundOpen);
	}

	if (keyboardModifiers.testFlag(Qt::ShiftModifier))
	{
		return (useNewTab ? NewTabOpen : NewWindowOpen);
	}

	if (hints.testFlag(NewTabOpen) && !hints.testFlag(NewWindowOpen))
	{
		return (useNewTab ? NewTabOpen : NewWindowOpen);
	}

	if (SettingsManager::getOption(SettingsManager::Browser_ReuseCurrentTabOption).toBool())
	{
		return CurrentTabOpen;
	}

	return hints;
}

WindowsManager::OpenHints WindowsManager::calculateOpenHints(const QVariantMap &parameters)
{
	if (!parameters.contains(QLatin1String("hints")))
	{
		return calculateOpenHints();
	}

	if (parameters[QLatin1String("hints")].type() == QVariant::Int)
	{
		return static_cast<OpenHints>(parameters[QLatin1String("hints")].toInt());
	}

	const QStringList rawHints(parameters[QLatin1String("hints")].toStringList());
	OpenHints hints(DefaultOpen);

	for (int i = 0; i < rawHints.count(); ++i)
	{
		QString hint(rawHints.at(i));
		hint[0] = hint[0].toUpper();

		if (hint == QLatin1String("Private"))
		{
			hints |= PrivateOpen;
		}
		else if (hint == QLatin1String("CurrentTab"))
		{
			hints |= CurrentTabOpen;
		}
		else if (hint == QLatin1String("NewTab"))
		{
			hints |= NewTabOpen;
		}
		else if (hint == QLatin1String("NewWindow"))
		{
			hints |= NewWindowOpen;
		}
		else if (hint == QLatin1String("Background"))
		{
			hints |= BackgroundOpen;
		}
		else if (hint == QLatin1String("End"))
		{
			hints |= EndOpen;
		}
	}

	return hints;
}

int WindowsManager::getWindowCount(bool onlyPrivate) const
{
	if (!onlyPrivate || m_isPrivate)
	{
		return m_mainWindow->getTabBar()->count();
	}

	int amount = 0;

	for (int i = 0; i < m_mainWindow->getTabBar()->count(); ++i)
	{
		if (getWindowByIndex(i)->isPrivate())
		{
			++amount;
		}
	}

	return amount;
}

int WindowsManager::getWindowIndex(quint64 identifier) const
{
	for (int i = 0; i < m_mainWindow->getTabBar()->count(); ++i)
	{
		Window *window(m_mainWindow->getTabBar()->getWindow(i));

		if (window && window->getIdentifier() == identifier)
		{
			return i;
		}
	}

	return -1;
}

bool WindowsManager::event(QEvent *event)
{
	if (event->type() == QEvent::LanguageChange)
	{
		Window *window(m_mainWindow->getWorkspace()->getActiveWindow());

		if (window)
		{
			emit titleChanged(window->getTitle().isEmpty() ? tr("Empty") : window->getTitle());
		}
	}

	return QObject::event(event);
}

bool WindowsManager::isPrivate() const
{
	return m_isPrivate;
}

bool WindowsManager::hasUrl(const QUrl &url, bool activate)
{
	for (int i = 0; i < m_mainWindow->getTabBar()->count(); ++i)
	{
		Window *window(getWindowByIndex(i));

		if (window && window->getUrl() == url)
		{
			if (activate)
			{
				setActiveWindowByIndex(i);
			}

			return true;
		}
	}

	return false;
}

}
