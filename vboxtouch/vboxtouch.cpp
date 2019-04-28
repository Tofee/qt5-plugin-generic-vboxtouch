/****************************************************************************
**
** Copyright (C) 2013 Jolla Ltd.
** Contact: Richard Braakman <richard.braakman@jollamobile.com>
**
** GNU Lesser General Public License Usage
** This file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3.0 as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU General Public License version 3.0 requirements will be
** met: http://www.gnu.org/copyleft/gpl.html.
**
****************************************************************************/

#include "vboxtouch.h"

#include <QDebug>
#include <QFileInfo>
#include <QGuiApplication>
#include <QSocketNotifier>
#include <QStringList>
#include <QTouchDevice>
#include <QLoggingCategory>

#include <qpa/qwindowsysteminterface.h>

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "evdevmousehandler.h"

Q_LOGGING_CATEGORY(qLcVBoxTouch, "qt.qpa.input")

VirtualboxTouchScreenHandler::VirtualboxTouchScreenHandler(const QString &specification, QObject *parent)
    : QObject(parent), m_fd(-1), m_notifier(0), m_device(0), m_failures(0), m_mouse(0),
      m_button(false), m_x(0), m_y(0)
{
    setObjectName("Virtualbox Touch Handler");

    QString specs = specification;
    QString env_specification = QString::fromLocal8Bit(qgetenv("VIRTUALBOX_TOUCH_EVDEV_MOUSE"));
    if(!env_specification.isEmpty()) {
        specs = env_specification;
    }
    
    QStringList devices = specs.split(':');

    if (devices.isEmpty()) {
        qCDebug(qLcEvdevMouse) << "evdevmouse: Using device discovery";
        QDeviceDiscovery *deviceDiscovery = QDeviceDiscovery::create(QDeviceDiscovery::Device_Mouse | QDeviceDiscovery::Device_Touchpad, this);
        if (deviceDiscovery) {
            // scan and add already connected keyboards
            devices = deviceDiscovery->scanConnectedDevices();
            
            delete deviceDiscovery; deviceDiscovery = nullptr;
        }
    }

    Q_FOREACH (QString evdev_device, devices) {
        qCDebug(qLcVBoxTouch) << "vboxtouch: Using evdev device " << qPrintable(evdev_device);

        EvdevMouseHandler *new_mouse = EvdevMouseHandler::create(evdev_device, "abs");
        if (!new_mouse) {
            qWarning("vboxtouch init: cannot open evdev mouse %s", qPrintable(evdev_device));
            shutdown();
            return;
        }
        connect(new_mouse, SIGNAL(handleMouseEvent(int, int, bool, Qt::MouseButtons, Qt::MouseButton, QEvent::Type)), 
                this, SLOT(handleEvdevInput(int, int, bool, Qt::MouseButtons, Qt::MouseButton, QEvent::Type)));
    }

    m_device = new QTouchDevice;
    m_device->setName("MergedMouseDevices");
    m_device->setType(QTouchDevice::TouchScreen);
    m_device->setCapabilities(QTouchDevice::Position);
    QWindowSystemInterface::registerTouchDevice(m_device);
}

VirtualboxTouchScreenHandler::~VirtualboxTouchScreenHandler()
{
    shutdown();
    // Cannot delete m_device because registerTouchDevice() holds a pointer.
}

void VirtualboxTouchScreenHandler::shutdown()
{
    qCDebug(qLcVBoxTouch) << "shutting down vboxtouch";
    if (m_notifier) {
        delete m_notifier;
        m_notifier = 0;
    }
    if (m_fd >= 0) {
        close(m_fd);
        m_fd = -1;
    }
    if  (m_mouse) {
        delete m_mouse;
        m_mouse = 0;
    }
}

void VirtualboxTouchScreenHandler::handleEvdevInput(int x, int y, bool abs, Qt::MouseButtons buttons, Qt::MouseButton button, QEvent::Type type)
{
    if(type == QEvent::MouseMove) {
        m_x = x;
        m_y = y;
        if (m_button) {
            reportTouch(Qt::TouchPointMoved);
        }
    }
    else {
        // Only the left button counts as a touch
        bool leftbutton = (buttons & Qt::LeftButton) != 0;
        if (leftbutton != m_button) {
            m_button = leftbutton;
            
            reportTouch(m_button ? Qt::TouchPointPressed : Qt::TouchPointReleased);
        }
    }
}

void VirtualboxTouchScreenHandler::reportTouch(Qt::TouchPointState state)
{
    QWindowSystemInterface::TouchPoint tp;

    QRect screen = QGuiApplication::primaryScreen()->geometry();
    qreal normal_x = (qreal) m_x / (qreal) (screen.width() - 1);
    qreal normal_y = (qreal) m_y / (qreal) (screen.height() - 1);

//    qCDebug(qLcVBoxTouch) << "vboxtouch: reportTouch at " << m_x <<","<<m_y<< " clicked: " << m_button;

    tp.pressure = m_button ? 1 : 0;
    tp.state = state;
    tp.normalPosition = QPointF(normal_x, normal_y);
    tp.area = QRectF(0, 0, 4, 4);
    tp.area.moveCenter(QPointF(normal_x * (screen.width() - 1),
                               normal_y * (screen.height() - 1)));
    tp.rawPositions.append(QPointF(m_x, m_y));

    QList<QWindowSystemInterface::TouchPoint> touchpoints;
    QWindowSystemInterface::handleTouchEvent(0, m_device, touchpoints << tp);
}
