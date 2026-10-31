#include "AudioEffects.h"
#include "EngineInternal.h"
QJsonArray Engine::audioEffectsJson(int index) const
{
    Lock lock(&m_mutex);
    const auto *l = const_cast<Engine *>(this)->layer(index);
    return l && l->audioEffects ? l->audioEffects->toJson() : QJsonArray();
}
bool Engine::setAudioEffectsJson(int index, const QJsonArray &state, QString *error, bool keepUnavailable)
{
    std::shared_ptr<AudioEffectChain> chain;
    {
        Lock lock(&m_mutex);
        auto *l = layer(index);
        if (!l || l->isGroup || l->isViewport) {
            if (error)
                *error = "Audio FX require a source layer";
            return false;
        }
        if (!l->audioEffects)
            l->audioEffects = std::make_shared<AudioEffectChain>();
        chain = l->audioEffects;
        // Existing streams already share this object; the callback sees chain edits atomically.
    }
    if (!chain->replace(state, error, keepUnavailable))
        return false;
    emit layersChanged();
    return true;
}
