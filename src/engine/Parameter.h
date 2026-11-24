#pragma once
// A parameter: one value of a layer, or of one of its shaders (its generator, an effect), declared once by the object
// that holds it — a Layer instantiates its own (Layer::parameters), an IsfInstance its own from its inputs
// (IsfInstance::parameters). Everything that works on parameters asks the composition for its layers, then each layer
// for its parameters: OSC (the address is the path), the timelines and the layers' animations (the animatable ones),
// the Animate menu, the inspector's limits and resets.
//
// This is the Qt face of pxs-core's parameter (pxs::core::tree::Parameter, one implementation for every application): the same
// declaration, the same behaviour, with QString / QVariant / QJsonValue at the edge, converted by pxs::convert. NyxMap's code
// keeps working on Qt types; what a parameter is and does is pxs-core's.

#include <pxs/Convert.h>
#include <pxs/core/tree/parameter.h>

#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <functional>

// What a parameter is, copied out of its holder (the interface works on these: a layer's parameters may be made again
// when its source or its effects change)
struct ParamInfo {
    using Core = pxs::core::tree::ParamInfo;
    using Type = Core::Type;
    using Ramp = Core::Ramp;
    enum Access { Read = Core::Read, Write = Core::Write, ReadWrite = Core::ReadWrite };

    QString path;  // its address in its layer ("opacity", "spatial/rotation", "fx/<fx>/param/<name>/x"…)
    QString label; // "Spatial › Rotation (°)": the category, then its name
    Type type = Type::Float;
    Ramp ramp = Ramp::Linear;
    int access = ReadWrite;
    double step = 0.01;      // its granularity: a number moves by this (a whole number: 1)
    QVariant defaultValue{};   // what a reset puts back
    double min = 0, max = 1; // the range shown (bars, lanes, OSC range)
    double lo = 0, hi = 1;   // the limits a value is kept within
    QList<int> values{};       // Int: the values it can take (empty: any whole number within the limits)
    QStringList choices{};     // Choice: its keys
    bool animatable = false; // a timeline or an animation can drive it (the numbers)
    int colorComponent = -1; // explicit RGB(A) member at <color path>/r,g,b,a; -1: not a color
    int pointComponent = -1; // explicit XY member at <point path>/x,y; -1: not a point
    bool stored = false;     // saved in a file and a snapshot (Parameter::isStored)
    bool presettable = false; // a preset can hold it: stored, a state of the show (Enable, Solo, Blackout), or an action
    bool action = false;      // an action rather than a value (a Trigger, or a value it can only be sent)
    bool signal = false;      // a stream the device emits, with no value of its own: it can be seen, not set
    bool unbounded = false;   // no range to tell (OSCQuery omits RANGE): the value is not limited to what is shown
    QString unit;         // what it measures, as OSCQuery names it ("distance.m", "angle.degree"); empty: none said
    QString extendedType; // what it is, as OSCQuery names it ("color.rgb.r", "filepath"); empty: said by its components

    bool isNumber() const { return type == Type::Float || type == Type::Int; }
    QString name() const; // its own name, without the category ("Rotation (°)")

    static ParamInfo fromCore(const Core &c);
    Core toCore() const;
};

class Parameter
{
public:
    using Type = ParamInfo::Type;
    using Ramp = ParamInfo::Ramp;
    Parameter(const QString &path, const QString &label, Type type);

    // Its declaration, set fluently where it is made
    Parameter &range(double min, double max) { m_core.range(min, max); return *this; } // shown; also the limits unless limits() says otherwise
    Parameter &limits(double lo, double hi) { m_core.limits(lo, hi); return *this; }
    Parameter &step(double s) { m_core.step(s); return *this; }
    Parameter &ramp(Ramp r) { m_core.ramp(r); return *this; }
    Parameter &readOnly() { m_core.readOnly(); return *this; }
    Parameter &writeOnly() { m_core.writeOnly(); return *this; }
    Parameter &unbounded() { m_core.unbounded(); return *this; } // no declared range, whatever the defaults
    Parameter &unit(const QString &name) { m_core.unit(pxs::convert::toUtf8(name)); return *this; }                  // OSCQuery UNIT
    Parameter &extendedType(const QString &name) { m_core.extendedType(pxs::convert::toUtf8(name)); return *this; } // OSCQuery EXTENDED_TYPE
    Parameter &transient() { m_core.transient(); return *this; } // read and written, never saved (a state of the show, not of a cue: on / off, solo)
    Parameter &animatable(bool on = true) { m_core.animatable(on); return *this; }
    Parameter &colorComponent(int component) { m_core.colorComponent(component); return *this; }
    Parameter &pointComponent(int component) { m_core.pointComponent(component); return *this; }
    Parameter &values(const QList<int> &v);           // Int: among these
    Parameter &choices(const QStringList &keys);      // Choice
    Parameter &byDefault(const QVariant &v);
    Parameter &byDefault(std::function<QVariant()> f); // a default that depends on where the holder is (pixels…)
    // Where its value lives: read and written in its holder (the holder's lock is held by the caller)
    Parameter &bind(std::function<QVariant()> get, std::function<void(const QVariant &)> set);

    // The same parameter under its holder's holder: path and the address under it ("fx/blur/" + "speed")
    Parameter prefixed(const QString &prefix) const
    {
        Parameter p = *this;
        p.m_core = m_core.prefixed(pxs::convert::toUtf8(prefix));
        p.m_path = prefix + m_path;
        return p;
    }
    ParamInfo info() const; // its declaration, its default resolved now
    const QString &path() const { return m_path; }
    bool isNumber() const { return m_core.isNumber(); }
    bool animatable() const { return m_core.animatable(); }
    int colorComponent() const { return m_core.colorComponent(); }

    QVariant value() const { return pxs::convert::toVariant(m_core.value()); }
    double number() const { return m_core.number(); }
    double asNumber() const { return m_core.asNumber(); } // what an Internal interaction follows: a number, a switch 0 / 1, a choice's index
    // Written within its limits and its granularity (an Int to the nearest of its values, a Choice only to one of its keys);
    // false if it cannot be written or the value is not one it takes
    bool setValue(const QVariant &v) { return m_core.setValue(pxs::convert::toValue(v)); } // tells the change feed when it was written
    // Whose it is (a layer's id; 0: the composition's, an interaction's, the show's, a fixture's): said with a written value
    Parameter &holder(quint64 id) { m_core.holder(id); return *this; }
    quint64 holder() const { return m_core.holder(); }
    bool setNumber(double v) { return m_core.setNumber(v); }
    bool reset() { return m_core.reset(); }

    // Its value in a file (a project, a snapshot, a copied layer): a number, a switch, a key, a text
    bool isStored() const { return m_core.isStored(); } // read and written, not an action
    QJsonValue json() const { return pxs::convert::toJson(m_core.stored()); }
    bool setJson(const QJsonValue &v) { return m_core.setStored(pxs::convert::fromJson(v)); }

    pxs::core::tree::Parameter &core() { return m_core; }
    const pxs::core::tree::Parameter &core() const { return m_core; }

private:
    pxs::core::tree::Parameter m_core;
    QString m_path;
};

// The values of a holder's parameters in a file, by address; read back in their order (an address the holder no longer
// has goes to `unknown`, when given)
template <class List> QJsonObject parametersToJson(const List &params)
{
    QJsonObject o;
    for (const auto &p : params)
        if (p->isStored()) o.insert(p->path(), p->json());
    return o;
}
template <class List> void parametersFromJson(const List &params, const QJsonObject &o, QJsonObject *unknown = nullptr)
{
    QJsonObject left = o;
    for (const auto &p : params) {
        const auto it = left.constFind(p->path());
        if (it == left.constEnd()) continue;
        if (p->isStored()) p->setJson(it.value());
        left.remove(p->path());
    }
    if (unknown) *unknown = left;
}
