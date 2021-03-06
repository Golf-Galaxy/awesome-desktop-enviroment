/****************************************
 *
 *   theShell - Desktop Environment
 *   Copyright (C) 2018 Victor Tran
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * *************************************/

#include "background.h"
#include "ui_background.h"

extern float getDPIScaling();

extern Background* firstBackground;

Background::Background(MainWindow* mainwindow, bool imageGetter, QRect screenGeometry, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::Background)
{
    ui->setupUi(this);
    this->mainwindow = mainwindow;
    this->imageGetter = imageGetter;

    this->screenGeometry = screenGeometry;
    screenGeometry.moveTo(0, 0);

    background = QPixmap(screenGeometry.size());
    background.fill(Qt::black);

    changeBackground();
    set = true;
}

Background::~Background()
{
    delete ui;
}

void Background::changeBackground() {
    QString backPath = settings.value("desktop/background", "inbuilt:triangles").toString();

    if (backPath.startsWith("inbuilt:")) { //Inbuilt background
        QSvgRenderer renderer(QString(":/backgrounds/" + backPath.split(":").at(1)));
        QPainter painter(&background);
        renderer.render(&painter, background.rect());
    } else if (backPath.startsWith("community")) {
        QDir::home().mkpath(".theshell/backgrounds");
        bool metadataExists = QFile(QDir::homePath() + "/.theshell/backgrounds.conf").exists();
        if (metadataExists) {
            if (imageGetter) {
                setNewBackgroundTimer();
            }
            loadCommunityBackgroundMetadata();
        } else {
            getNewCommunityBackground();
        }
        return;
    } else {
        QPixmap image;
        image.load(backPath);
        QPainter painter(&background);

        //Clear background
        painter.setBrush(QColor(0, 0, 0));
        painter.setPen(Qt::transparent);
        painter.drawRect(0, 0, background.width(), background.height());

        //Draw background
        switch (settings.value("desktop/stretchStyle", 0).toInt()) {
            case 0: //Stretch
                painter.drawPixmap(0, 0, background.width(), background.height(), image);
                break;
            case 1: { //Zoom and Crop
                QRect rect;
                rect.setSize(image.size().scaled(background.width(), background.height(), Qt::KeepAspectRatioByExpanding));
                rect.moveLeft(background.width() / 2 - rect.width() / 2);
                rect.moveTop(background.height() / 2 - rect.height() / 2);
                painter.drawPixmap(rect, image);
                break;
            }
            case 2: { //Center
                QRect rect;
                rect.setSize(image.size());
                rect.moveLeft(background.width() / 2 - rect.width() / 2);
                rect.moveTop(background.height() / 2 - rect.height() / 2);
                painter.drawPixmap(rect, image);
                break;
            }
            case 3: //Tile
                painter.drawTiledPixmap(0, 0, background.width(), background.height(), image);
                break;
            case 4: { //Zoom and Fit
                QRect rect;
                rect.setSize(image.size().scaled(background.width(), background.height(), Qt::KeepAspectRatio));
                rect.moveLeft(background.width() / 2 - rect.width() / 2);
                rect.moveTop(background.height() / 2 - rect.height() / 2);
                painter.drawPixmap(rect, image);
                break;
            }
        }
        //background = background.scaled(screenGeometry.size());
    }

    this->update();
}

void Background::getNewCommunityBackground() {
    if (!imageGetter) return;

    QNetworkRequest req(QUrl("https://vicr123.github.io/theshell/backgrounds/backgrounds.json"));
    req.setHeader(QNetworkRequest::UserAgentHeader, QString("theShell/") + TS_VERSION);
    req.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
    QNetworkReply* listOfBackgrounds = manager.get(req);
    connect(listOfBackgrounds, &QNetworkReply::finished, [=] {
        QByteArray data = listOfBackgrounds->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (!doc.isArray()) {
            //error error
            if (QFile(QDir::homePath() + "/.theshell/background.conf").exists()) {
                //We have a valid image we can use
                //Try getting a new image tomorrow
                QDateTime oldFetchedValue = settings.value("desktop/fetched").toDateTime();
                oldFetchedValue = oldFetchedValue.addDays(1);
                settings.setValue("desktop/fetched", oldFetchedValue);
                setNewBackgroundTimer();
                loadCommunityBackgroundMetadata();
            } else {
                //ui->label->setText(tr("Couldn't get community backgrounds!"));
            }
        } else {
            QJsonArray arr = doc.array();
            arr.removeFirst();

            QStringList downloadImages;
            qsrand(QDateTime::currentMSecsSinceEpoch());

            for (int i = 0; i < 10; i++) {
                int index = qrand() % arr.count();
                QString url = arr.at(index).toString();
                downloadImages.append(url);
                arr.removeAt(index);

                if (arr.count() == 0) {
                    i = 10;
                }
            }

            //Keep track of time when these images were retrieved
            settings.setValue("desktop/fetched", QDateTime::currentDateTimeUtc());

            //Delete list of known images
            QFile(QDir::homePath() + "/.theshell/backgrounds.conf").remove();
            QDir(QDir::homePath() + "/.theshell/backgrounds/").removeRecursively();
            QDir::home().mkpath(".theshell/backgrounds/");

            numberDone = 0;
            for (QString url : downloadImages) {
                QNetworkRequest req((QUrl("https://vicr123.github.io" + url)));
                req.setHeader(QNetworkRequest::UserAgentHeader, QString("theShell/") + TS_VERSION);
                req.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
                QNetworkReply* metadata = manager.get(req);
                connect(metadata, &QNetworkReply::finished, [=] {
                    QByteArray data = metadata->readAll();
                    QJsonDocument doc = QJsonDocument::fromJson(data);
                    if (doc.isObject()) {
                        QJsonObject obj = doc.object();

                        QString fileName = obj.value("filename").toString();
                        QString dirName = fileName.left(fileName.indexOf("."));

                        QDir::home().mkpath(".theshell/backgrounds/" + dirName);
                        QFile metadataFile(QDir::homePath() + "/.theshell/backgrounds/" + dirName + "/metadata.json");
                        metadataFile.open(QFile::WriteOnly);
                        metadataFile.write(data);
                        metadataFile.close();

                        QFile backgroundListConf(QDir::homePath() + "/.theshell/backgrounds.conf");
                        backgroundListConf.open(QFile::Append);
                        backgroundListConf.write(dirName.append("\n").toUtf8());
                        backgroundListConf.close();
                    }
                    metadata->deleteLater();
                    numberDone++;

                    if (numberDone == downloadImages.count()) {
                        numberDone = 0;
                        setNewBackgroundTimer();

                        //Load up a new background from the cache
                        loadCommunityBackgroundMetadata();
                    }
                });
            }
        }
        listOfBackgrounds->deleteLater();
    });
}

void Background::loadCommunityBackgroundMetadata() {
    if (!imageGetter) return;

    QFile backgroundListConf(QDir::homePath() + "/.theshell/backgrounds.conf");
    backgroundListConf.open(QFile::ReadOnly);
    QStringList allBackgrounds = QString(backgroundListConf.readAll()).split("\n");
    backgroundListConf.close();

    allBackgrounds.removeAll("");

    qsrand(QDateTime::currentMSecsSinceEpoch());
    QString background = allBackgrounds.at(qrand() % allBackgrounds.count());

    if (QFile(QDir::homePath() + "/.theshell/backgrounds/" + background + "/" + background + ".jpeg").exists()) {
        QTimer::singleShot(0, [=] {
            emit setAllBackgrounds(background);
            setCommunityBackground(background);
        });;
    } else {
        QFile metadataFile(QDir::homePath() + "/.theshell/backgrounds/" + background + "/metadata.json");
        metadataFile.open(QFile::ReadOnly);
        QJsonDocument doc = QJsonDocument::fromJson(metadataFile.readAll());
        metadataFile.close();

        if (doc.isObject()) {
            QString fileName = doc.object().value("filename").toString();
            QString dirName = fileName.left(fileName.indexOf("."));

            QNetworkRequest req(QUrl(QString("https://vicr123.github.io/theshell/backgrounds/%1/%2").arg(dirName, fileName)));
            req.setHeader(QNetworkRequest::UserAgentHeader, QString("theShell/") + TS_VERSION);
            req.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
            QNetworkReply* image = manager.get(req);
            connect(image, &QNetworkReply::finished, [=] {
                if (image->error() == QNetworkReply::NoError) {
                    QByteArray data = image->readAll();
                    QFile imageFile(QDir::homePath() + "/.theshell/backgrounds/" + background + "/" + background + ".jpeg");
                    imageFile.open(QFile::WriteOnly);
                    imageFile.write(data);
                    imageFile.close();
                    image->deleteLater();

                    if (set) {
                        emit reloadBackground();
                    } else {
                        //ui->stackedWidget->setCurrentIndex(0);
                        setCommunityBackground(background);
                        if (imageGetter) {
                            emit setAllBackgrounds(background);
                        }
                    }
                } else {
                    //Error retrieving image
                    //Try another image
                    loadCommunityBackgroundMetadata();
                    image->deleteLater();
                }
            });
        } else {
            //Error reading metadata file
            //Clear and start again
            getNewCommunityBackground();
        }
    }
}

void Background::setCommunityBackground(QString bg) {
    QFile metadataFile(QDir::homePath() + "/.theshell/backgrounds/" + bg + "/metadata.json");
    QFile imageFile(QDir::homePath() + "/.theshell/backgrounds/" + bg + "/" + bg + ".jpeg");
    if (!metadataFile.exists()) {
        getNewCommunityBackground();
        return;
    }
    if (!imageFile.exists()) {
        loadCommunityBackgroundMetadata();
        return;
    }

    metadataFile.open(QFile::ReadOnly);
    QJsonDocument doc = QJsonDocument::fromJson(metadataFile.readAll());
    if (doc.isObject()) {
        QJsonObject metadata = doc.object();
        QPixmap image;
        image.load(imageFile.fileName());

        QPainter painter(&this->background);

        //Clear background
        painter.setBrush(QColor(0, 0, 0));
        painter.setPen(Qt::transparent);
        painter.drawRect(0, 0, background.width(), background.height());

        //Draw background
        switch (settings.value("desktop/stretchStyle", 0).toInt()) {
            case 0: //Stretch
                painter.drawPixmap(0, 0, background.width(), background.height(), image);
                break;
            case 1: { //Zoom and Crop
                QRect rect;
                rect.setSize(image.size().scaled(background.width(), background.height(), Qt::KeepAspectRatioByExpanding));
                rect.moveLeft(background.width() / 2 - rect.width() / 2);
                rect.moveTop(background.height() / 2 - rect.height() / 2);
                painter.drawPixmap(rect, image);
                break;
            }
            case 2: { //Center
                QRect rect;
                rect.setSize(image.size());
                rect.moveLeft(background.width() / 2 - rect.width() / 2);
                rect.moveTop(background.height() / 2 - rect.height() / 2);
                painter.drawPixmap(rect, image);
                break;
            }
            case 3: //Tile
                painter.drawTiledPixmap(0, 0, background.width(), background.height(), image);
                break;
            case 4: { //Zoom and Fit
                QRect rect;
                rect.setSize(image.size().scaled(background.width(), background.height(), Qt::KeepAspectRatio));
                rect.moveLeft(background.width() / 2 - rect.width() / 2);
                rect.moveTop(background.height() / 2 - rect.height() / 2);
                painter.drawPixmap(rect, image);
                break;
            }
        }
        //painter.drawImage(0, 0, image.scaled(background.width(), background.height(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation));

        if (settings.value("desktop/showLabels", true).toBool()) {
            QLinearGradient darkener;
            darkener.setColorAt(0, QColor::fromRgb(0, 0, 0, 0));
            darkener.setColorAt(1, QColor::fromRgb(0, 0, 0, 200));

            if (settings.value("bar/onTop").toBool()) {
                darkener.setStart(0, 0);
                darkener.setFinalStop(0, background.height());
            } else {
                darkener.setStart(0, background.height());
                darkener.setFinalStop(0, 0);
            }
            painter.setBrush(darkener);
            painter.drawRect(0, 0, background.width(), background.height());

            painter.setPen(Qt::white);
            int currentX = 30 * getDPIScaling();
            int baselineY;

            if (settings.value("bar/onTop").toBool()) {
                baselineY = background.height() - 30 * getDPIScaling();
            } else {
                baselineY = 30 * getDPIScaling() + QFontMetrics(QFont(this->font().family(), 20)).ascent();
            }

            if (metadata.contains("name")) {
                painter.setFont(QFont(this->font().family(), 20));
                QString name = metadata.value("name").toString();
                int width = painter.fontMetrics().width(name);
                painter.drawText(currentX, baselineY, name);

                currentX += width + 9 * getDPIScaling();
            }


            if (metadata.contains("location")) {
                painter.setFont(QFont(this->font().family(), 10));
                QIcon locationIcon = QIcon::fromTheme("gps");
                QString location = metadata.value("location").toString();
                int height = painter.fontMetrics().height();
                int width = painter.fontMetrics().width(location) + height;

                painter.drawPixmap(currentX, baselineY - height, locationIcon.pixmap(16 * getDPIScaling(), 16 * getDPIScaling()));
                painter.drawText(currentX + height + 6 * getDPIScaling(), baselineY - painter.fontMetrics().descent(), location);

                currentX += width + 20 * getDPIScaling();
            }

            if (metadata.contains("author")) {
                painter.setFont(QFont(this->font().family(), 10));
                QString author = tr("by %1").arg(metadata.value("author").toString());
                int width = painter.fontMetrics().width(author);
                painter.drawText(background.width() - width - 30 * getDPIScaling(), baselineY, author);
            }
        }

        painter.end();
        this->update();
        currentBackground = bg;

        if (imageGetter) {
            settings.setValue("desktop/changed", QDateTime::currentDateTimeUtc());
            setTimer();
        }
    } else {
        getNewCommunityBackground();
    }
}

void Background::show() {
    Atom DesktopWindowTypeAtom;
    DesktopWindowTypeAtom = XInternAtom(QX11Info::display(), "_NET_WM_WINDOW_TYPE_DESKTOP", False);
    XChangeProperty(QX11Info::display(), this->winId(), XInternAtom(QX11Info::display(), "_NET_WM_WINDOW_TYPE", False),
                     XA_ATOM, 32, PropModeReplace, (unsigned char*) &DesktopWindowTypeAtom, 1);
    QDialog::show();
}

void Background::setTimer() {
    if (!imageGetter) return;

    if (timer != nullptr) timer->deleteLater();
    QDateTime fetchedTime = settings.value("desktop/changed").toDateTime();
    int waitTime = settings.value("desktop/waitTime", 30).toInt();
    QDateTime tickTime = fetchedTime.addSecs(waitTime * 60);

    if (tickTime < QDateTime::currentDateTimeUtc()) {
        //Get the next image now
        loadCommunityBackgroundMetadata();
    } else {
        timer = new QTimer();
        if (tickTime.toMSecsSinceEpoch() - QDateTime::currentMSecsSinceEpoch() > INT_MAX) {
            timer->setInterval(INT_MAX);
            connect(timer, SIGNAL(timeout()), this, SLOT(setTimer()));
        } else {
            timer->setInterval(tickTime.toMSecsSinceEpoch() - QDateTime::currentMSecsSinceEpoch());
            connect(timer, SIGNAL(timeout()), this, SLOT(loadCommunityBackgroundMetadata()));
        }
        timer->setSingleShot(true);
        timer->start();
    }
}

void Background::setNewBackgroundTimer() {
    if (!imageGetter) return;

    if (newBackgroundTimer != nullptr) newBackgroundTimer->deleteLater();
    QDateTime fetchedTime = settings.value("desktop/fetched").toDateTime();
    QDateTime tickTime = fetchedTime.addDays(7);
    //QDateTime tickTime = fetchedTime.addSecs(120);

    if (tickTime < QDateTime::currentDateTimeUtc()) {
        //Get the next image now
        getNewCommunityBackground();
    } else {
        newBackgroundTimer = new QTimer();
        if (tickTime.toMSecsSinceEpoch() - QDateTime::currentMSecsSinceEpoch() > INT_MAX) {
            newBackgroundTimer->setInterval(INT_MAX);
            connect(newBackgroundTimer, SIGNAL(timeout()), this, SLOT(setNewBackgroundTimer()));
        } else {
            newBackgroundTimer->setInterval(tickTime.toMSecsSinceEpoch() - QDateTime::currentMSecsSinceEpoch());
            connect(newBackgroundTimer, SIGNAL(timeout()), this, SLOT(getNewCommunityBackground()));
        }
        newBackgroundTimer->setSingleShot(true);
        newBackgroundTimer->start();
    }
}

void Background::on_graphicsView_customContextMenuRequested(const QPoint &pos)
{
}

void Background::on_actionOpen_Status_Center_triggered()
{
    mainwindow->getInfoPane()->show(InfoPaneDropdown::Clock);
}

void Background::on_actionOpen_theShell_Settings_triggered()
{
    mainwindow->getInfoPane()->show(InfoPaneDropdown::Settings);
}

void Background::on_actionOpen_System_Settings_triggered()
{
    QProcess::startDetached("systemsettings5");
}

void Background::on_actionChange_Background_triggered()
{
    ChooseBackground *background = new ChooseBackground(currentBackground);
    connect(background, SIGNAL(reloadBackgrounds()), mainwindow, SIGNAL(reloadBackgrounds()));
    connect(background, SIGNAL(reloadTimer()), firstBackground, SLOT(setTimer()));
    background->show();
}

void Background::reject() {

}

void Background::on_Background_customContextMenuRequested(const QPoint &pos)
{
    QMenu *menu = new QMenu(this);
    menu->addSection(tr("For desktop"));
    menu->addAction(ui->actionChange_Background);
    menu->addSection(tr("For system"));
    menu->addAction(ui->actionOpen_theShell_Settings);
    menu->addAction(ui->actionOpen_Status_Center);

    menu->exec(this->mapToGlobal(pos));
}

void Background::paintEvent(QPaintEvent *event) {
    QPainter p(this);
    p.drawPixmap(0, 0, this->background);
}
