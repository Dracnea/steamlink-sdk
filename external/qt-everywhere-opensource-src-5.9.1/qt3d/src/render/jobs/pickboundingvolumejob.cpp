/****************************************************************************
**
** Copyright (C) 2015 Paul Lemire paul.lemire350@gmail.com
** Contact: https://www.qt.io/licensing/
**
** This file is part of the Qt3D module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl-3.0.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 2.0 or (at your option) the GNU General
** Public license version 3 or any later version approved by the KDE Free
** Qt Foundation. The licenses are as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL2 and LICENSE.GPL3
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-2.0.html and
** https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "pickboundingvolumejob_p.h"
#include "qpicktriangleevent.h"
#include <Qt3DRender/private/renderer_p.h>
#include <Qt3DRender/private/nodemanagers_p.h>
#include <Qt3DRender/private/entity_p.h>
#include <Qt3DRender/private/objectpicker_p.h>
#include <Qt3DRender/private/managers_p.h>
#include <Qt3DRender/private/geometryrenderer_p.h>

#include <Qt3DRender/private/rendersettings_p.h>
#include <Qt3DRender/qgeometryrenderer.h>
#include <Qt3DRender/private/job_common_p.h>
#include <Qt3DRender/private/qpickevent_p.h>

QT_BEGIN_NAMESPACE

namespace Qt3DRender {
using namespace Qt3DRender::RayCasting;

namespace Render {

namespace {

typedef PickingUtils::AbstractCollisionGathererFunctor::result_type HitList;

void setEventButtonAndModifiers(const QMouseEvent &event, QPickEvent::Buttons &eventButton, int &eventButtons, int &eventModifiers)
{
    switch (event.button()) {
    case Qt::LeftButton:
        eventButton = QPickEvent::LeftButton;
        break;
    case Qt::RightButton:
        eventButton = QPickEvent::RightButton;
        break;
    case Qt::MiddleButton:
        eventButton = QPickEvent::MiddleButton;
        break;
    case Qt::BackButton:
        eventButton = QPickEvent::BackButton;
        break;
    default:
        break;
    }

    if (event.buttons() & Qt::LeftButton)
        eventButtons |= QPickEvent::LeftButton;
    if (event.buttons() & Qt::RightButton)
        eventButtons |= QPickEvent::RightButton;
    if (event.buttons() & Qt::MiddleButton)
        eventButtons |= QPickEvent::MiddleButton;
    if (event.buttons() & Qt::BackButton)
        eventButtons |= QPickEvent::BackButton;
    if (event.modifiers() & Qt::ShiftModifier)
        eventModifiers |= QPickEvent::ShiftModifier;
    if (event.modifiers() & Qt::ControlModifier)
        eventModifiers |= QPickEvent::ControlModifier;
    if (event.modifiers() & Qt::AltModifier)
        eventModifiers |= QPickEvent::AltModifier;
    if (event.modifiers() & Qt::MetaModifier)
        eventModifiers |= QPickEvent::MetaModifier;
    if (event.modifiers() & Qt::KeypadModifier)
        eventModifiers |= QPickEvent::KeypadModifier;
}

} // anonymous

PickBoundingVolumeJob::PickBoundingVolumeJob()
    : m_manager(nullptr)
    , m_node(nullptr)
    , m_frameGraphRoot(nullptr)
    , m_renderSettings(nullptr)
    , m_pickersDirty(true)
{
    SET_JOB_RUN_STAT_TYPE(this, JobTypes::PickBoundingVolume, 0);
}

void PickBoundingVolumeJob::setRoot(Entity *root)
{
    m_node = root;
}

void PickBoundingVolumeJob::setMouseEvents(const QList<QMouseEvent> &pendingEvents)
{
    m_pendingMouseEvents = pendingEvents;
}

void PickBoundingVolumeJob::setKeyEvents(const QList<QKeyEvent> &pendingEvents)
{
    m_pendingKeyEvents = pendingEvents;
}

void PickBoundingVolumeJob::setFrameGraphRoot(FrameGraphNode *frameGraphRoot)
{
    m_frameGraphRoot = frameGraphRoot;
}

void PickBoundingVolumeJob::setRenderSettings(RenderSettings *settings)
{
    m_renderSettings = settings;
}

RayCasting::QRay3D PickBoundingVolumeJob::intersectionRay(const QPoint &pos, const QMatrix4x4 &viewMatrix,
                                                          const QMatrix4x4 &projectionMatrix, const QRect &viewport)
{
    QVector3D nearPos = QVector3D(pos.x(), pos.y(), 0.0f);
    nearPos = nearPos.unproject(viewMatrix, projectionMatrix, viewport);
    QVector3D farPos = QVector3D(pos.x(), pos.y(), 1.0f);
    farPos = farPos.unproject(viewMatrix, projectionMatrix, viewport);

    return RayCasting::QRay3D(nearPos,
                              (farPos - nearPos).normalized(),
                              (farPos - nearPos).length());
}

bool PickBoundingVolumeJob::runHelper()
{
    // Move to clear the events so that we don't process them several times
    // if run is called several times
    const auto mouseEvents = std::move(m_pendingMouseEvents);

    // If we have no events return early
    if (mouseEvents.empty())
        return false;

    // Quickly look which picker settings we've got
    if (m_pickersDirty) {
        m_pickersDirty = false;
        m_oneEnabledAtLeast = false;
        m_oneHoverAtLeast = false;

        const auto activeHandles = m_manager->objectPickerManager()->activeHandles();
        for (auto handle : activeHandles) {
            auto picker = m_manager->objectPickerManager()->data(handle);
            m_oneEnabledAtLeast |= picker->isEnabled();
            m_oneHoverAtLeast |= picker->isHoverEnabled();
            if (m_oneEnabledAtLeast && m_oneHoverAtLeast)
                break;
        }
    }

    // bail out early if no picker is enabled
    if (!m_oneEnabledAtLeast)
        return false;

    bool hasMoveEvent = false;
    bool hasOtherEvent = false;
    // Quickly look which types of events we've got
    for (const QMouseEvent &event : mouseEvents) {
        const bool isMove = (event.type() == QEvent::MouseMove);
        hasMoveEvent |= isMove;
        hasOtherEvent |= !isMove;
    }

    // In the case we have a move event, find if we actually have
    // an object picker that cares about these
    if (!hasOtherEvent) {
        // Retrieve the last used object picker
        ObjectPicker *lastCurrentPicker = m_manager->objectPickerManager()->data(m_currentPicker);

        // The only way to set lastCurrentPicker is to click
        // so we can return since if we're there it means we
        // have only move events. But keep on if hover support
        // is needed
        if (lastCurrentPicker == nullptr && !m_oneHoverAtLeast)
            return false;

        const bool caresAboutMove = (hasMoveEvent &&
                                      (m_oneHoverAtLeast ||
                                        (lastCurrentPicker && lastCurrentPicker->isDragEnabled())));
        // Early return if the current object picker doesn't care about move events
        if (!caresAboutMove)
            return false;
    }

    PickingUtils::ViewportCameraAreaGatherer vcaGatherer;
    // TO DO: We could cache this and only gather when we know the FrameGraph tree has changed
    const QVector<PickingUtils::ViewportCameraAreaTriplet> vcaTriplets = vcaGatherer.gather(m_frameGraphRoot);

    // If we have no viewport / camera or area, return early
    if (vcaTriplets.empty())
        return false;

    // TO DO:
    // If we have move or hover move events that someone cares about, we try to avoid expensive computations
    // by compressing them into a single one

    // Store the reducer function which varies depending on the picking settings set on the renderer
    using ReducerFunction = PickingUtils::CollisionVisitor::HitList (*)(PickingUtils::CollisionVisitor::HitList &results, const PickingUtils::CollisionVisitor::HitList &intermediate);

    const bool trianglePickingRequested = (m_renderSettings->pickMethod() == QPickingSettings::TrianglePicking);
    const bool allHitsRequested = (m_renderSettings->pickResultMode() == QPickingSettings::AllPicks);
    const bool frontFaceRequested =
            m_renderSettings->faceOrientationPickingMode() != QPickingSettings::BackFace;
    const bool backFaceRequested =
            m_renderSettings->faceOrientationPickingMode() != QPickingSettings::FrontFace;

    // Select the best reduction function based on the settings
    const ReducerFunction reducerOp = allHitsRequested ? PickingUtils::reduceToAllHits : PickingUtils::reduceToFirstHit;

    // For each mouse event
    for (const QMouseEvent &event : mouseEvents) {
        m_hoveredPickersToClear = m_hoveredPickers;

        QPickEvent::Buttons eventButton = QPickEvent::NoButton;
        int eventButtons = 0;
        int eventModifiers = QPickEvent::NoModifier;

        setEventButtonAndModifiers(event, eventButton, eventButtons, eventModifiers);

        // For each triplet of Viewport / Camera and Area
        for (const PickingUtils::ViewportCameraAreaTriplet &vca : vcaTriplets) {
            HitList sphereHits;
            QRay3D ray = rayForViewportAndCamera(vca.area, event.pos(), vca.viewport, vca.cameraId);

            PickingUtils::HierarchicalEntityPicker entityPicker(ray);
            if (entityPicker.collectHits(m_node)) {
                if (trianglePickingRequested) {
                    PickingUtils::TriangleCollisionGathererFunctor gathererFunctor;
                    gathererFunctor.m_frontFaceRequested = frontFaceRequested;
                    gathererFunctor.m_backFaceRequested = backFaceRequested;
                    gathererFunctor.m_manager = m_manager;
                    gathererFunctor.m_ray = ray;
                    sphereHits = QtConcurrent::blockingMappedReduced<HitList>(entityPicker.entities(),
                                                                              gathererFunctor, reducerOp);
                } else {
                    sphereHits = entityPicker.hits();
                    PickingUtils::AbstractCollisionGathererFunctor::sortHits(sphereHits);
                    if (!allHitsRequested)
                        sphereHits = { sphereHits.front() };
                }
            }

            // Dispatch events based on hit results
            dispatchPickEvents(event, sphereHits, eventButton, eventButtons, eventModifiers,
                               trianglePickingRequested, allHitsRequested);
        }
    }

    // Clear Hovered elements that needs to be cleared
    // Send exit event to object pickers on which we
    // had set the hovered flag for a previous frame
    // and that aren't being hovered any longer
    clearPreviouslyHoveredPickers();
    return true;
}

void PickBoundingVolumeJob::setManagers(NodeManagers *manager)
{
    m_manager = manager;
}

void PickBoundingVolumeJob::markPickersDirty()
{
    m_pickersDirty = true;
}

void PickBoundingVolumeJob::run()
{
    Q_ASSERT(m_frameGraphRoot && m_renderSettings && m_node && m_manager);
    runHelper();
}

void PickBoundingVolumeJob::dispatchPickEvents(const QMouseEvent &event,
                                               const PickingUtils::CollisionVisitor::HitList &sphereHits,
                                               QPickEvent::Buttons eventButton,
                                               int eventButtons,
                                               int eventModifiers,
                                               bool trianglePickingRequested,
                                               bool allHitsRequested)
{
    ObjectPicker *lastCurrentPicker = m_manager->objectPickerManager()->data(m_currentPicker);
    // If we have hits
    if (!sphereHits.isEmpty()) {
        // Note: how can we control that we want the first/last/all elements along the ray to be picked

        // How do we differentiate betwnee an Entity with no GeometryRenderer and one with one, both having
        // an ObjectPicker component when it comes

        for (const QCollisionQueryResult::Hit &hit : qAsConst(sphereHits)) {
            Entity *entity = m_manager->renderNodesManager()->lookupResource(hit.m_entityId);
            HObjectPicker objectPickerHandle = entity->componentHandle<ObjectPicker, 16>();

            // If the Entity which actually received the hit doesn't have
            // an object picker component, we need to check the parent if it has one ...
            while (objectPickerHandle.isNull() && entity != nullptr) {
                entity = entity->parent();
                if (entity != nullptr)
                    objectPickerHandle = entity->componentHandle<ObjectPicker, 16>();
            }

            ObjectPicker *objectPicker = m_manager->objectPickerManager()->data(objectPickerHandle);
            if (objectPicker != nullptr && objectPicker->isEnabled()) {

                if (lastCurrentPicker && !allHitsRequested) {
                    // if there is a current picker, it will "grab" all events until released.
                    // Clients should test that entity is what they expect (or only use
                    // world coordinates)
                    objectPicker = lastCurrentPicker;
                }

                // Send the corresponding event
                QVector3D localIntersection = hit.m_intersection;
                if (entity && entity->worldTransform())
                    localIntersection = entity->worldTransform()->inverted() * hit.m_intersection;

                QPickEventPtr pickEvent;
                if (trianglePickingRequested) {
                    pickEvent.reset(new QPickTriangleEvent(event.localPos(), hit.m_intersection, localIntersection, hit.m_distance,
                                                           hit.m_triangleIndex, hit.m_vertexIndex[0], hit.m_vertexIndex[1], hit.m_vertexIndex[2],
                            eventButton, eventButtons, eventModifiers, hit.m_uvw));
                    QPickEventPrivate::get(pickEvent.data())->m_entity = hit.m_entityId;
                } else {
                    pickEvent.reset(new QPickEvent(event.localPos(), hit.m_intersection, localIntersection, hit.m_distance,
                                                   eventButton, eventButtons, eventModifiers));
                }
                switch (event.type()) {
                case QEvent::MouseButtonPress: {
                    // Store pressed object handle
                    m_currentPicker = objectPickerHandle;
                    // Send pressed event to m_currentPicker
                    objectPicker->onPressed(pickEvent);
                    break;
                }

                case QEvent::MouseButtonRelease: {
                    // Only send the release event if it was pressed
                    if (objectPicker->isPressed())
                        objectPicker->onReleased(pickEvent);
                    if (lastCurrentPicker != nullptr && m_currentPicker == objectPickerHandle) {
                        objectPicker->onClicked(pickEvent);
                        m_currentPicker = HObjectPicker();
                    }
                    break;
                }
#if QT_CONFIG(gestures)
                case Qt::TapGesture: {
                    objectPicker->onClicked(pickEvent);
                    break;
                }
#endif
                case QEvent::MouseMove: {
                    if ((objectPicker->isPressed() || objectPicker->isHoverEnabled()) && objectPicker->isDragEnabled()) {
                        objectPicker->onMoved(pickEvent);
                    }
                    Q_FALLTHROUGH(); // fallthrough
                }
                case QEvent::HoverMove: {
                    if (!m_hoveredPickers.contains(objectPickerHandle)) {
                        if (objectPicker->isHoverEnabled()) {
                            // Send entered event to objectPicker
                            objectPicker->onEntered();
                            // and save it in the hoveredPickers
                            m_hoveredPickers.push_back(objectPickerHandle);
                        }
                    }
                    break;
                }

                default:
                    break;
                }
            }

            // The ObjectPicker was hit -> it is still being hovered
            m_hoveredPickersToClear.removeAll(objectPickerHandle);

            lastCurrentPicker = m_manager->objectPickerManager()->data(m_currentPicker);
        }

        // Otherwise no hits
    } else {
        switch (event.type()) {
        case QEvent::MouseButtonRelease: {
            // Send release event to m_currentPicker
            if (lastCurrentPicker != nullptr) {
                m_currentPicker = HObjectPicker();
                QPickEventPtr pickEvent(new QPickEvent);
                lastCurrentPicker->onReleased(pickEvent);
            }
            break;
        }
        default:
            break;
        }
    }
}

void PickBoundingVolumeJob::viewMatrixForCamera(Qt3DCore::QNodeId cameraId,
                                                QMatrix4x4 &viewMatrix,
                                                QMatrix4x4 &projectionMatrix) const
{
    Render::CameraLens *lens = nullptr;
    Entity *camNode = m_manager->renderNodesManager()->lookupResource(cameraId);
    if (camNode != nullptr &&
            (lens = camNode->renderComponent<CameraLens>()) != nullptr &&
            lens->isEnabled()) {
        viewMatrix = *camNode->worldTransform();
        projectionMatrix = lens->projection();
    }
}

QRect PickBoundingVolumeJob::windowViewport(const QSize &area, const QRectF &relativeViewport) const
{
    if (area.isValid()) {
        const int areaWidth = area.width();
        const int areaHeight = area.height();
        return QRect(relativeViewport.x() * areaWidth,
                     (1.0 - relativeViewport.y() - relativeViewport.height()) * areaHeight,
                     relativeViewport.width() * areaWidth,
                     relativeViewport.height() * areaHeight);
    }
    return relativeViewport.toRect();
}

RayCasting::QRay3D PickBoundingVolumeJob::rayForViewportAndCamera(const QSize &area,
                                                                  const QPoint &pos,
                                                                  const QRectF &relativeViewport,
                                                                  const Qt3DCore::QNodeId cameraId) const
{
    QMatrix4x4 viewMatrix;
    QMatrix4x4 projectionMatrix;
    viewMatrixForCamera(cameraId, viewMatrix, projectionMatrix);
    const QRect viewport = windowViewport(area, relativeViewport);

    // In GL the y is inverted compared to Qt
    const QPoint glCorrectPos = QPoint(pos.x(), area.isValid() ? area.height() - pos.y() : pos.y());
    const auto ray = intersectionRay(glCorrectPos, viewMatrix, projectionMatrix, viewport);
    return ray;
}

void PickBoundingVolumeJob::clearPreviouslyHoveredPickers()
{
    for (const HObjectPicker pickHandle : qAsConst(m_hoveredPickersToClear)) {
        ObjectPicker *pick = m_manager->objectPickerManager()->data(pickHandle);
        if (pick)
            pick->onExited();
        m_hoveredPickers.removeAll(pickHandle);
    }
    m_hoveredPickersToClear.clear();
}

} // namespace Render

} // namespace Qt3DRender

QT_END_NAMESPACE
