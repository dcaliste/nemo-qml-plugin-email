/*
 * Copyright (C) 2013 Jolla Ltd.
 * Contact: Valerio Valerio <valerio.valerio@jollamobile.com>
 *
 * This program is licensed under the terms and conditions of the
 * Apache License, version 2.0.  The full text of the Apache License is at
 * http://www.apache.org/licenses/LICENSE-2.0
 */

#include <QVector>
#include <QDir>
#include <QFile>

#include "attachmentlistmodel.h"
#include "emailagent.h"

AttachmentListModel::AttachmentListModel(QObject *parent) :
    QAbstractListModel(parent)
  , m_messageId(QMailMessageId())
  , m_attachmentFileWatcher(nullptr)
{
    roles.insert(ContentLocation, "contentLocation");
    roles.insert(DisplayName, "displayName");
    roles.insert(Downloaded, "downloaded");
    roles.insert(MimeType, "mimeType");
    roles.insert(Size, "size");
    roles.insert(StatusInfo, "statusInfo");
    roles.insert(Url, "url");
    roles.insert(ProgressInfo, "progressInfo");

    connect(EmailAgent::instance(), SIGNAL(attachmentDownloadStatusChanged(QString,EmailAgent::AttachmentStatus)),
            this, SLOT(onAttachmentDownloadStatusChanged(QString,EmailAgent::AttachmentStatus)));

    connect(EmailAgent::instance(), SIGNAL(attachmentDownloadProgressChanged(QString,int)),
            this, SLOT(onAttachmentDownloadProgressChanged(QString,int)));

    connect(EmailAgent::instance(), SIGNAL(attachmentUrlChanged(QString,QString)),
            this, SLOT(onAttachmentUrlChanged(QString,QString)));
}

AttachmentListModel::~AttachmentListModel()
{
}

QHash<int, QByteArray> AttachmentListModel::roleNames() const
{
    return roles;
}

int AttachmentListModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return m_attachmentsList.count();
}

QVariant AttachmentListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() > m_attachmentsList.count())
        return QVariant();

    const Attachment *item = static_cast<const Attachment *>(index.internalPointer());
    Q_ASSERT(item);

    if (role == ContentLocation) {
        return item->location;
    } else if (role == DisplayName) {
        return EmailAgent::instance()->attachmentName(item->part);
    } else if (role == Downloaded) {
        if (item->status == EmailAgent::Downloaded) {
            return true;
        } else {
            //Addresses the case where content size is missing
            return item->part.contentAvailable() || item->part.contentDisposition().size() <= 0;
        }
    } else if (role == MimeType) {
        return QString::fromLatin1(item->part.contentType().content());
    } else if (role == Size) {
        if (item->part.contentDisposition().size() != -1) {
            return item->part.contentDisposition().size();
        }
        // If size is -1 (unknown) try finding out part's body size
        if (item->part.contentAvailable()) {
            return item->part.hasBody() ? item->part.body().length() : 0;
        }
        return -1;
    } else if (role == StatusInfo) {
        return item->status;
    } else if (role == Url) {
        return item->url;
    } else if (role == ProgressInfo) {
        return item->progressInfo;
    }
    return QVariant();
}

QModelIndex AttachmentListModel::index(int row, int column, const QModelIndex &parent) const
{
    Q_UNUSED (column);
    Q_UNUSED (parent);

    if (-1 < row && row < m_attachmentsList.count()) {
        return m_attachmentsList[row]->index;
    }

    qCWarning(lcGeneral) << Q_FUNC_INFO << "Row " << row << "is not present in the model";
    return QModelIndex();
}

QModelIndex AttachmentListModel::indexFromLocation(const QString &location)
{
    for (const Attachment *item : m_attachmentsList) {
        if (item->location == location) {
            return item->index;
        }
    }
    return QModelIndex();
}

void AttachmentListModel::onAttachmentDownloadStatusChanged(const QString &attachmentLocation, EmailAgent::AttachmentStatus status)
{
    for (Attachment *attachment : m_attachmentsList) {
        if (attachment->location == attachmentLocation) {
            attachment->status = status;

            QVector<int> updateRoles;
            updateRoles << StatusInfo;
            if (status == EmailAgent::NotDownloaded) {
                attachment->url.clear();
                attachment->progressInfo = 0;
                updateRoles << Url << ProgressInfo;
            }

            emit dataChanged(attachment->index, attachment->index, updateRoles);
            return;
        }
    }
}

void AttachmentListModel::onAttachmentDownloadProgressChanged(const QString &attachmentLocation, int progress)
{
    for (Attachment *attachment : m_attachmentsList) {
        if (attachment->location == attachmentLocation) {
            attachment->progressInfo = progress;
            emit dataChanged(attachment->index, attachment->index, QVector<int>() << ProgressInfo);
            return;
        }
    }
}

void AttachmentListModel::onAttachmentUrlChanged(const QString &attachmentLocation, const QString &url)
{
    for (Attachment *attachment : m_attachmentsList) {
        if (attachment->location == attachmentLocation) {
            if (attachment->url != url) {
                attachment->url = url;
                emit dataChanged(attachment->index, attachment->index, QVector<int>() << Url);
                return;
            }
        }
    }
}

static bool findPartFromAttachment(const QMailMessagePart &part, const QString &attachmentLocation, QMailMessagePart &found)
{
    if (part.multipartType() == QMailMessagePart::MultipartNone) {
        if (attachmentLocation == part.location().toString(true)) {
            found = part;
            return true;
        }
    } else {
        for (uint i = 0; i < part.partCount(); i++) {
            if (findPartFromAttachment(part.partAt(i), attachmentLocation, found))
                return true;
        }
    }
    return false;
}

QString AttachmentListModel::attachmentUrl(const QMailMessage &message, const QString &attachmentLocation) const
{
    // Temporary attachments must be saved in a account specific folder to enable easy cleaning of them
    QString attachmentDownloadFolder = Downloads::folder(message, attachmentLocation);
    for (uint i = 0; i < message.partCount(); i++) {
        QMailMessagePart part = message.partAt(i);
        QMailMessagePart sourcePart;
        if (findPartFromAttachment(part, attachmentLocation, sourcePart)) {
            QString attachmentPath = attachmentDownloadFolder + "/" + sourcePart.displayName();
            QFile f(attachmentPath);
            if (f.exists()) {
                return attachmentPath;
            } else {
                return QString();
            }
        }
    }
    return QString();
}

QString AttachmentListModel::displayName(int idx)
{
    return data(index(idx,0), DisplayName).toString();
}

bool AttachmentListModel::downloadStatus(int idx)
{
    return data(index(idx,0), Downloaded).toBool();
}

QString AttachmentListModel::mimeType(int idx)
{
    return data(index(idx,0), MimeType).toString();
}

QString AttachmentListModel::url(int idx)
{
    return data(index(idx,0), Url).toString();
}

int AttachmentListModel::count() const
{
    return rowCount();
}

int AttachmentListModel::messageId() const
{
    return m_messageId.toULongLong();
}

void AttachmentListModel::setMessageId(int id)
{
    m_messageId = QMailMessageId(id);
    m_message = QMailMessage(m_messageId);
    resetModel();
}

void AttachmentListModel::resetModel()
{
    beginResetModel();
    qDeleteAll(m_attachmentsList.begin(), m_attachmentsList.end());
    m_attachmentsList.clear();

    delete m_attachmentFileWatcher;
    m_attachmentFileWatcher = new QFileSystemWatcher(this);

    connect(m_attachmentFileWatcher, &QFileSystemWatcher::directoryChanged, this, [this]() {
        for (const QMailMessagePart::Location &location :  m_message.findAttachmentLocations()) {
            QString attachmentLocation = location.toString(true);
            QString url = attachmentUrl(m_message, attachmentLocation);
            onAttachmentUrlChanged(attachmentLocation, url);
        }
    });

    if (m_messageId.isValid()) {
        int i=0;
        for (const QMailMessagePart::Location &location :  m_message.findAttachmentLocations()) {
            Attachment *item = new Attachment;
            item->location = location.toString(true);
            QString dlFolder = Downloads::folder(m_message, item->location);
            QDir::root().mkpath(dlFolder);
            m_attachmentFileWatcher->addPath(dlFolder);
            item->part = m_message.partAt(location);
            item->status = EmailAgent::instance()->attachmentDownloadStatus(item->location);
            // if attachment is in the queue for download we will get a url update later

            EmailAgent::instance()->addAttachmentToDownloads(item->location, m_messageId);

            if (item->status == EmailAgent::NotDownloaded) {
                item->url = attachmentUrl(m_message, item->location);
                // Update status and progress if attachment exists
                if (!item->url.isEmpty() || item->part.hasBody()) {
                    item->status = EmailAgent::Downloaded;
                    item->progressInfo = 100;
                    EmailAgent::instance()->updateAttachmenDownloadStatus(item->location, EmailAgent::Downloaded);
                } else {
                    item->progressInfo = 0;
                }
            } else {
                item->url = QString();
                item->progressInfo = EmailAgent::instance()->attachmentDownloadProgress(item->location);
            }
            item->index = createIndex(i, 0, item);
            m_attachmentsList.append(item);
            i++;
        }
    }
    endResetModel();
    emit countChanged();
}
