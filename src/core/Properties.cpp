#include "core/Properties.h"

#include "core/Logger.h"
#include "core/Scripting.h"

namespace Falcor
{
Properties::Properties() : Properties(json::object()) {}

Properties::Properties(const json& value) : Properties(json(value)) {}

Properties::Properties(json&& value) : mJson(std::make_unique<json>(std::move(value))) { CHECK(mJson->is_object()); }

Properties::Properties(PyObject* dict) : Properties()
{
    if (!dict || !PyDict_Check(dict))
    {
        PyErr_SetString(PyExc_TypeError, "properties must be a dict");
        return;
    }

    const PythonObject module{PyImport_ImportModule("json")};
    const PythonObject encoded{module ? PyObject_CallMethod(module.get(), "dumps", "O", dict) : nullptr};
    const auto* source = encoded ? PyUnicode_AsUTF8(encoded.get()) : nullptr;
    if (!source)
        return;

    auto value = json::parse(source, nullptr, false);
    if (!value.is_object())
    {
        PyErr_SetString(PyExc_ValueError, "invalid render pass properties");
        return;
    }
    *mJson = std::move(value);
}

Properties::Properties(const Properties& other) : mJson(std::make_unique<json>(*other.mJson)) {}
Properties::Properties(Properties&& other) noexcept = default;
Properties::~Properties() = default;

Properties& Properties::operator=(const Properties& other)
{
    mJson = std::make_unique<json>(*other.mJson);
    return *this;
}

Properties& Properties::operator=(Properties&& other) noexcept = default;

Properties::json Properties::toJson() const { return *mJson; }

PyObject* Properties::toPython() const
{
    Scripting::start();
    const auto encoded = dump();
    const PythonObject module{PyImport_ImportModule("json")};
    return module ? PyObject_CallMethod(module.get(), "loads", "s", encoded.c_str()) : nullptr;
}

std::string Properties::dump(int indent) const { return mJson->dump(indent); }
bool Properties::empty() const { return mJson->empty(); }
bool Properties::has(std::string_view name) const { return mJson->contains(name); }
bool Properties::operator==(const Properties& other) const { return *mJson == *other.mJson; }

struct Properties::Iterator::Impl
{
    Properties& properties;
    json::iterator it;
};

Properties::Iterator::Iterator(std::unique_ptr<Impl> impl) : mImpl(std::move(impl)) {}
Properties::Iterator::~Iterator() = default;

bool Properties::Iterator::operator==(const Iterator& other) const { return mImpl->it == other.mImpl->it; }

Properties::Iterator& Properties::Iterator::operator++()
{
    ++mImpl->it;
    return *this;
}

std::pair<std::string, Properties::Value> Properties::Iterator::operator*()
{
    const auto& name = mImpl->it.key();
    return {name, mImpl->properties[name]};
}

struct Properties::ConstIterator::Impl
{
    const Properties& properties;
    json::const_iterator it;
};

Properties::ConstIterator::ConstIterator(std::unique_ptr<Impl> impl) : mImpl(std::move(impl)) {}
Properties::ConstIterator::~ConstIterator() = default;

bool Properties::ConstIterator::operator==(const ConstIterator& other) const { return mImpl->it == other.mImpl->it; }

Properties::ConstIterator& Properties::ConstIterator::operator++()
{
    ++mImpl->it;
    return *this;
}

std::pair<std::string, Properties::ConstValue> Properties::ConstIterator::operator*()
{
    const auto& name = mImpl->it.key();
    return {name, mImpl->properties[name]};
}

Properties::Iterator Properties::begin()
{
    return Iterator(std::make_unique<Iterator::Impl>(Iterator::Impl{*this, mJson->begin()}));
}

Properties::Iterator Properties::end()
{
    return Iterator(std::make_unique<Iterator::Impl>(Iterator::Impl{*this, mJson->end()}));
}

Properties::ConstIterator Properties::begin() const
{
    return ConstIterator(std::make_unique<ConstIterator::Impl>(ConstIterator::Impl{*this, mJson->cbegin()}));
}

Properties::ConstIterator Properties::end() const
{
    return ConstIterator(std::make_unique<ConstIterator::Impl>(ConstIterator::Impl{*this, mJson->cend()}));
}
}  // namespace Falcor
