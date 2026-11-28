#include "Engine.h"
#include "InteractionOutput.h"

#include <QJsonArray>
#include <algorithm>

void Engine::updateDeviceConfiguration()
{
    const QJsonObject json = m_devices->toJson();
    std::map<quint64, pxs::devices::Device> dmx;
    for (const auto *device : m_devices->all())
        if (device->kind == pxs::devices::Kind::Dmx) dmx.emplace(device->id, *device);

    bool edited;
    {
        Lock lock(&m_mutex);
        edited = json.value("devices") != m_deviceJson.value("devices");
        m_deviceJson = json;
        m_dmxDevices = std::move(dmx);
        // A device can become reachable after the interaction's source has stopped moving.
        for (const auto &interaction : m_interactions) {
            if (interaction.layer || !interaction.param.startsWith(QLatin1String("output/device/"))) continue;
            const auto run = m_interactionRun.find(interaction.id);
            if (run != m_interactionRun.end()) run->second.lastOut.reset();
        }
        for (const auto &node : m_interactionGraph.nodes)
            if (!node.settings.layer && node.settings.param.startsWith(QLatin1String("output/device/")))
                m_graphLastOutputs.erase(node.id);
        for (auto *levels : {&m_deviceDmxActions, &m_timelineDeviceDmx}) {
            for (auto it = levels->begin(); it != levels->end();) {
                const auto device = m_dmxDevices.find(it->first.first);
                if (device == m_dmxDevices.end() || !device->second.address(it->first.second.toStdString()))
                    it = levels->erase(it);
                else
                    ++it;
            }
        }
    }
    if (edited) emit deviceConfigurationChanged();
}

void Engine::loadDevices(const QJsonObject &json)
{
    m_devices->setReferencedDevices({});
    m_devices->fromJson(json);
    if (!json.value("remembered_oscquery").toArray().isEmpty()) m_devices->startDiscovery();
}

bool Engine::holdsDmxOutput(const QString &path) const
{
    if (path.startsWith(QLatin1String("output/dmx/"))) return true;
    return path.startsWith(QLatin1String("output/device/")) &&
           m_dmxDevices.count(InteractionOutput::fromPath(path).destination.toULongLong());
}

void Engine::writeDeviceOutput(quint64 id, const QString &address, double value, bool action)
{
    if (!id || address.isEmpty()) return;
    const double unit = std::clamp(value, 0.0, 1.0);
    if (m_dmxDevices.count(id)) {
        auto &levels = action ? m_deviceDmxActions : m_deviceDmx;
        levels[{id, address}] = unit;
        return;
    }

    queueDeviceValue(id, address, unit, true);
}

void Engine::queueDeviceValue(quint64 id, const QString &address, double value, bool normalized)
{
    // Interactions and timelines run on the render and cue threads. PXS sockets and their registry live on the owner thread.
    const quint64 generation = m_deviceProjectGeneration.load();
    QMetaObject::invokeMethod(this, [this, id, address, value, normalized, generation] {
        if (generation != m_deviceProjectGeneration.load() || !m_devices->online(id)) return;
        double nativeValue = value;
        if (normalized) {
            const auto parameters = m_devices->params(id);
            const auto parameter = std::find_if(parameters.begin(), parameters.end(),
                                                [&](const auto &entry) { return entry.path == address; });
            if (parameter == parameters.end()) return;
            nativeValue = parameter->lo + value * (parameter->hi - parameter->lo);
        }
        if (m_devices->send(id, address, nativeValue)) {
            Lock lock(&m_mutex);
            if (generation == m_deviceProjectGeneration.load()) m_deviceLastSent[{id, address}] = nativeValue;
        }
    }, Qt::QueuedConnection);
}
