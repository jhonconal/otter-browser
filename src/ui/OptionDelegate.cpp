/**************************************************************************
* Otter Browser: Web browser controlled by the user, not vice-versa.
* Copyright (C) 2013 - 2017 Michal Dutkiewicz aka Emdek <michal@emdek.pl>
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

#include "OptionDelegate.h"
#include "../core/SettingsManager.h"

#include <QtGui/QPainter>

namespace Otter
{

OptionDelegate::OptionDelegate(bool isSimple, QObject *parent) : ItemDelegate(parent),
	m_isSimple(isSimple)
{
}

void OptionDelegate::initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const
{
	ItemDelegate::initStyleOption(option, index);

	const SettingsManager::OptionDefinition definition(SettingsManager::getOptionDefinition(SettingsManager::getOptionIdentifier(index.data(Qt::UserRole).toString())));

	switch (definition.type)
	{
		case SettingsManager::BooleanType:
			option->text = (index.data(Qt::DisplayRole).toBool() ? tr("Yes") : tr("No"));

			break;
		case SettingsManager::ColorType:
			{
				const QColor color(index.data(Qt::DisplayRole).toString());
				QPixmap icon(option->fontMetrics.height(), option->fontMetrics.height());
				icon.fill(Qt::transparent);

				QPainter iconPainter(&icon);
				iconPainter.setRenderHints(QPainter::Antialiasing);

				if (color.alpha() < 255)
				{
					QPixmap pixmap(10, 10);
					pixmap.fill(Qt::white);

					QPainter pixmapPainter(&pixmap);
					pixmapPainter.setBrush(Qt::gray);
					pixmapPainter.setPen(Qt::NoPen);
					pixmapPainter.drawRect(0, 0, 5, 5);
					pixmapPainter.drawRect(5, 5, 5, 5);
					pixmapPainter.end();

					iconPainter.setBrush(pixmap);
					iconPainter.setPen(Qt::NoPen);
					iconPainter.drawRoundedRect(icon.rect(), 2, 2);
				}

				iconPainter.setBrush(color);
				iconPainter.setPen(option->palette.color(QPalette::Button));
				iconPainter.drawRoundedRect(icon.rect(), 2, 2);
				iconPainter.end();

				option->features |= QStyleOptionViewItem::HasDecoration;
				option->decorationSize = icon.size();
				option->icon = QIcon(icon);
				option->text = index.data(Qt::DisplayRole).toString().toUpper();
			}

			break;
		case SettingsManager::EnumerationType:
			{
				const QString value(index.data(Qt::DisplayRole).toString());

				if (definition.hasIcons())
				{
					option->features |= QStyleOptionViewItem::HasDecoration;
				}

				for (int i = 0; i < definition.choices.count(); ++i)
				{
					if (definition.choices.at(i).value == value)
					{
						option->text = definition.choices.at(i).getTitle();
						option->icon = definition.choices.at(i).icon;

						break;
					}
				}
			}

			break;
		case SettingsManager::FontType:
			option->font = QFont(index.data(Qt::DisplayRole).toString());

			break;
		case SettingsManager::PasswordType:
			if (!option->text.isEmpty())
			{
				option->text = QString(5, QChar(0x2022));
			}

			break;
		default:
			break;
	}
}

void OptionDelegate::updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
	Q_UNUSED(option)
	Q_UNUSED(index)

	editor->setGeometry(option.rect);
}

void OptionDelegate::setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const
{
	if (m_isSimple)
	{
		OptionWidget *widget(qobject_cast<OptionWidget*>(editor));

		if (widget)
		{
			model->setData(index, widget->getValue());
		}
	}
}

QWidget* OptionDelegate::createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
	Q_UNUSED(option)

	const QString name(index.data(Qt::UserRole).toString());
	const int identifier(SettingsManager::getOptionIdentifier(name));
	const SettingsManager::OptionDefinition definition(SettingsManager::getOptionDefinition(identifier));
	QVariant value(index.data(Qt::EditRole));

	if (value.isNull())
	{
		value = SettingsManager::getOption(identifier);
	}

	OptionWidget *widget(new OptionWidget(name, value, definition.type, parent));
	widget->setIndex(index);
	widget->setButtons(m_isSimple ? OptionWidget::NoButtons : (OptionWidget::ResetButton | OptionWidget::SaveButton));

	if (definition.type == SettingsManager::EnumerationType)
	{
		widget->setChoices(definition.choices);
	}

	connect(widget, SIGNAL(commitData(QWidget*)), this, SIGNAL(commitData(QWidget*)));

	return widget;
}

}
