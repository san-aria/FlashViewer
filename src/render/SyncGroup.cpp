#include "render/SyncGroup.hpp"

SyncGroup::SyncGroup(QObject* parent) : QObject(parent) {}

void SyncGroup::syncCamera(const Camera& cam, const std::string& src_wkt) {
    m_camera = cam;
    m_src_wkt = src_wkt;
    emit cameraChanged(m_camera, QString::fromStdString(m_src_wkt));
}
