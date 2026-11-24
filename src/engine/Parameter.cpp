#include "Parameter.h"

#include "ChangeFeed.h"

using pxs::convert::fromUtf8;
using pxs::convert::toUtf8;
using pxs::convert::toValue;
using pxs::convert::toVariant;

ParamInfo ParamInfo::fromCore(const Core &c)
{
    ParamInfo i;
    i.path = fromUtf8(c.path);
    i.label = fromUtf8(c.label);
    i.type = c.type;
    i.ramp = c.ramp;
    i.access = c.access;
    i.step = c.step;
    i.defaultValue = toVariant(c.defaultValue);
    i.min = c.min;
    i.max = c.max;
    i.lo = c.lo;
    i.hi = c.hi;
    for (int v : c.values) i.values << v;
    for (const std::string &s : c.choices) i.choices << fromUtf8(s);
    i.animatable = c.animatable;
    i.colorComponent = c.colorComponent;
    i.pointComponent = c.pointComponent;
    i.stored = c.stored;
    i.presettable = c.presettable;
    i.action = c.action;
    i.signal = c.signal;
    i.unbounded = c.unbounded;
    i.unit = fromUtf8(c.unit);
    i.extendedType = fromUtf8(c.extendedType);
    return i;
}

ParamInfo::Core ParamInfo::toCore() const
{
    Core c;
    c.path = toUtf8(path);
    c.label = toUtf8(label);
    c.type = type;
    c.ramp = ramp;
    c.access = access;
    c.step = step;
    c.defaultValue = toValue(defaultValue);
    c.min = min;
    c.max = max;
    c.lo = lo;
    c.hi = hi;
    for (int v : values) c.values.push_back(v);
    for (const QString &s : choices) c.choices.push_back(toUtf8(s));
    c.animatable = animatable;
    c.colorComponent = colorComponent;
    c.pointComponent = pointComponent;
    c.stored = stored;
    c.presettable = presettable;
    c.action = action;
    c.signal = signal;
    c.unbounded = unbounded;
    c.unit = toUtf8(unit);
    c.extendedType = toUtf8(extendedType);
    return c;
}

QString ParamInfo::name() const
{
    return fromUtf8(toCore().name());
}

Parameter::Parameter(const QString &path, const QString &label, Type type)
    : m_core(toUtf8(path), toUtf8(label), type), m_path(path)
{
    m_core.feed(&changeFeed()); // what is written is told to the observer of the application (ChangeFeed.h)
}

Parameter &Parameter::values(const QList<int> &v)
{
    m_core.values(std::vector<int>(v.cbegin(), v.cend()));
    return *this;
}

Parameter &Parameter::choices(const QStringList &keys)
{
    std::vector<std::string> out;
    for (const QString &k : keys) out.push_back(toUtf8(k));
    m_core.choices(std::move(out));
    return *this;
}

Parameter &Parameter::byDefault(const QVariant &v)
{
    m_core.byDefault(toValue(v));
    return *this;
}

Parameter &Parameter::byDefault(std::function<QVariant()> f)
{
    m_core.byDefault(pxs::core::tree::Parameter::Getter([f] { return toValue(f()); }));
    return *this;
}

Parameter &Parameter::bind(std::function<QVariant()> get, std::function<void(const QVariant &)> set)
{
    pxs::core::tree::Parameter::Getter g;
    pxs::core::tree::Parameter::Setter s;
    if (get) g = [get] { return toValue(get()); };
    if (set) s = [set](const pxs::core::Value &v) { set(toVariant(v)); };
    m_core.bind(std::move(g), std::move(s));
    return *this;
}

ParamInfo Parameter::info() const
{
    return ParamInfo::fromCore(m_core.info());
}
