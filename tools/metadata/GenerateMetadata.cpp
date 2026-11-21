#include <clang/AST/ASTConsumer.h>
#include <clang/AST/ASTContext.h>
#include <clang/AST/Attr.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/QualTypeNames.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendAction.h>
#include <clang/Tooling/CompilationDatabase.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Format.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/Path.h>
#include <llvm/Support/ToolOutputFile.h>

#include <cmath>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace
{
using clang::AnnotateAttr;
using clang::CXXRecordDecl;
using llvm::StringRef;

constexpr auto kRecord = "renderlab.record";
constexpr auto kRegister = "renderlab.register";
constexpr auto kField = "renderlab.field";
constexpr auto kEnum = "renderlab.enum";
constexpr auto kFlags = "renderlab.flags";
constexpr auto kEnumValue = "renderlab.enumValue";
constexpr auto kAllowUnknownFields = "renderlab.allowUnknownFields";
constexpr auto kShaderLayout = "renderlab.shaderLayout";

struct Header
{
    std::string source;
    std::string output;
    std::string declarations;
};

struct Registration
{
    std::string type;
    std::string header;
};

struct Metadata
{
    std::map<std::string, Header> headers;
    std::map<std::string, std::map<std::string, Registration>> registries;
};

std::string realPath(const llvm::Twine& path)
{
    llvm::SmallString<256> real;
    if (llvm::sys::fs::real_path(path, real))
        return {};

    return llvm::sys::path::convert_to_slash(real);
}

std::string outputPath(StringRef root, StringRef file)
{
    llvm::SmallString<256> path(root);
    llvm::sys::path::append(path, file);
    return llvm::sys::path::convert_to_slash(path);
}

const AnnotateAttr* annotation(const clang::Decl& declaration, StringRef name)
{
    for (const auto* attribute : declaration.specific_attrs<AnnotateAttr>())
        if (attribute->getAnnotation() == name)
            return attribute;

    return nullptr;
}

bool hasSchema(const clang::Decl& declaration)
{
    return annotation(declaration, kRecord) || annotation(declaration, kRegister);
}

llvm::raw_ostream& quoted(llvm::raw_ostream& out, StringRef text)
{
    out << '"';
    out.write_escaped(text);
    return out << '"';
}

void error(const clang::Decl& declaration, const llvm::Twine& message)
{
    auto& diagnostics = declaration.getASTContext().getDiagnostics();
    const auto id = diagnostics.getCustomDiagID(clang::DiagnosticsEngine::Error, "%0");
    diagnostics.Report(declaration.getLocation(), id) << message.str();
}

StringRef keyArgument(const clang::Decl& declaration, const AnnotateAttr& attribute, unsigned index)
{
    if (index < attribute.args_size())
    {
        const auto* argument = attribute.args_begin()[index]->IgnoreUnlessSpelledInSource();
        if (const auto* literal = llvm::dyn_cast<clang::StringLiteral>(argument))
        {
            const auto key = literal->getBytes();
            if (literal->isOrdinary() && !key.empty() && !key.contains('\0'))
                return key;
        }
    }

    error(declaration, "metadata keys must be nonempty string literals");
    return {};
}

std::optional<double> numberArgument(const clang::FieldDecl& declaration, const clang::Expr& expression)
{
    clang::Expr::EvalResult result;
    if (expression.EvaluateAsRValue(result, declaration.getASTContext()))
    {
        if (result.Val.isInt())
            return result.Val.getInt().roundToDouble();
        if (result.Val.isFloat())
        {
            auto number = result.Val.getFloat();
            bool losesInfo;
            number.convert(llvm::APFloat::IEEEdouble(), llvm::APFloat::rmNearestTiesToEven, &losesInfo);
            return number.convertToDouble();
        }
    }

    error(declaration, "slider bounds must be constant numbers");
    return std::nullopt;
}

bool derivesFrom(const CXXRecordDecl& declaration, StringRef target)
{
    if (declaration.getQualifiedNameAsString() == target)
        return true;

    for (const auto& base : declaration.bases())
        if (const auto* record = base.getType()->getAsCXXRecordDecl(); record && derivesFrom(*record, target))
            return true;

    return false;
}

std::string registryParent(const CXXRecordDecl& declaration, StringRef root)
{
    if (declaration.getQualifiedNameAsString() == root)
        return "void";

    std::string parent;
    for (const auto& base : declaration.bases())
    {
        const auto* record = base.getType()->getAsCXXRecordDecl();
        if (base.getAccessSpecifier() != clang::AS_public || !record || !derivesFrom(*record, root))
            continue;

        if (!parent.empty())
            error(declaration, "a registered type must have a single public parent in its registry");
        parent = "::" + record->getQualifiedNameAsString();
    }

    if (parent.empty())
        error(declaration, "registered type must publicly derive from " + root);

    return parent;
}

bool addressable(const clang::FieldDecl& field)
{
    if (field.getAccess() == clang::AS_public && !field.isBitField() && field.getIdentifier())
        return true;

    error(field, "metadata fields must be named public members that can be addressed");
    return false;
}

void writeField(llvm::raw_ostream& out, const clang::FieldDecl& field, const AnnotateAttr& attribute,
                StringRef owner, StringRef key)
{
    if (!addressable(field))
        return;
    if (attribute.args_size() == 1)
    {
        out << "        Field<&" << owner << "::" << field.getName() << ">{";
        quoted(out, key) << "},\n";
        return;
    }

    const auto minimum = numberArgument(field, *attribute.args_begin()[1]);
    const auto maximum = numberArgument(field, *attribute.args_begin()[2]);
    if (!minimum || !maximum)
        return;
    if (!std::isfinite(*minimum) || !std::isfinite(*maximum) || *minimum > *maximum)
    {
        error(field, "slider bounds must be finite and ordered");
        return;
    }

    out << "        SliderField<&" << owner << "::" << field.getName() << ">{{";
    quoted(out, key) << "}, {" << llvm::format("%.17g", *minimum) << ", "
                     << llvm::format("%.17g", *maximum) << "}},\n";
}

void writeLayout(llvm::raw_ostream& out, const CXXRecordDecl& declaration, StringRef type)
{
    out << "template <>\nstruct Layout<" << type << ">\n{\n"
        << "    static constexpr std::array<LayoutField, "
        << std::distance(declaration.field_begin(), declaration.field_end()) << "> kFields{{\n";
    for (const auto* field : declaration.fields())
    {
        if (!addressable(*field))
            continue;
        const auto name = field->getName();
        quoted(out << "        {", name) << ", offsetof(" << type << ", " << name << "), sizeof(decltype("
                                        << type << "::" << name << "))},\n";
    }
    out << "    }};\n};\n\n";
}

class Scan : public clang::RecursiveASTVisitor<Scan>
{
public:
    explicit Scan(Metadata& metadata) : metadata(metadata) {}

    bool VisitCXXRecordDecl(CXXRecordDecl* declaration)
    {
        const bool layout = annotation(*declaration, kShaderLayout);
        if (!declaration->isThisDeclarationADefinition() || declaration->isImplicit() ||
            (!layout && !hasSchema(*declaration)))
            return true;
        auto* header = findHeader(*declaration);
        if (!header)
            return true;

        if (!declaration->getIdentifier() || declaration->isInAnonymousNamespace() ||
            declaration->getDeclContext()->isFunctionOrMethod() || declaration->getDeclContext()->isDependentContext())
        {
            error(*declaration, "metadata types must have a qualified name available outside their declaration");
            return true;
        }

        const auto* typeTemplate = declaration->getDescribedClassTemplate();
        auto type = "::" + declaration->getQualifiedNameAsString();
        llvm::raw_string_ostream out(header->declarations);
        if (layout)
        {
            if (typeTemplate)
                error(*declaration, "shader layouts require a concrete record type");
            else
                writeLayout(out, *declaration, type);
            if (!hasSchema(*declaration))
                return true;
        }

        const auto* registration = annotation(*declaration, kRegister);
        const auto* record = annotation(*declaration, kRecord);
        std::string templateParameters;
        if (typeTemplate)
        {
            if (registration)
            {
                error(*declaration, "register concrete types instead of class templates");
                return true;
            }

            type += '<';
            for (const auto* parameter : *typeTemplate->getTemplateParameters())
            {
                const auto* typeParameter = llvm::dyn_cast<clang::TemplateTypeParmDecl>(parameter);
                if (!typeParameter || !typeParameter->getIdentifier() || typeParameter->isParameterPack() ||
                    typeParameter->hasTypeConstraint())
                {
                    error(*declaration, "metadata class templates require named unconstrained type parameters");
                    return true;
                }
                if (!templateParameters.empty())
                {
                    templateParameters += ", ";
                    type += ", ";
                }
                templateParameters += "class " + typeParameter->getNameAsString();
                type += typeParameter->getNameAsString();
            }
            type += '>';
        }

        out << "template <" << templateParameters << ">\nstruct Schema<" << type << ">\n{\n";
        if (registration)
        {
            const auto root = keyArgument(*declaration, *registration, 0);
            const auto key = keyArgument(*declaration, *registration, 1);
            const auto parent = registryParent(*declaration, root);
            if (!root.empty() && !key.empty() && !parent.empty())
            {
                auto& types = metadata.registries[root.str()];
                if (!types.emplace(key.str(), Registration{type, header->source}).second)
                    error(*declaration, "duplicate registered type ID " + key);
                out << "    using Parent = " << parent << ";\n";
                quoted(out << "    static constexpr auto kTypeId = ", key) << ";\n";
            }
        }
        else if (record->args_size() != 0)
        {
            const auto key = keyArgument(*declaration, *record, 0);
            quoted(out << "    static constexpr auto kTypeId = ", key) << ";\n";
        }

        std::vector<std::string> bases;
        for (const auto& base : declaration->bases())
        {
            const auto* baseType = base.getType()->getAsCXXRecordDecl();
            if (base.getAccessSpecifier() == clang::AS_public && baseType && hasSchema(*baseType))
                bases.push_back(clang::TypeName::getFullyQualifiedName(
                    base.getType(), declaration->getASTContext(), declaration->getASTContext().getPrintingPolicy(), true));
        }

        const bool allowUnknownFields = annotation(*declaration, kAllowUnknownFields);
        out << "    static constexpr bool kAllowUnknownFields = " << (allowUnknownFields ? "true" : "false");
        if (!allowUnknownFields)
            for (const auto& base : bases)
                out << " || Schema<" << base << ">::kAllowUnknownFields";
        out << ";\n    static constexpr auto kFields = ";
        if (!bases.empty())
        {
            out << "std::tuple_cat(\n";
            for (const auto& base : bases)
                out << "        Schema<" << base << ">::kFields,\n";
            out << "        ";
        }
        out << "std::tuple{\n";

        std::set<StringRef> keys;
        for (const auto* field : declaration->fields())
        {
            const auto* attribute = annotation(*field, kField);
            if (!attribute)
                continue;
            const auto key = keyArgument(*field, *attribute, 0);
            if (key.empty())
                continue;
            if (!keys.insert(key).second)
                error(*field, "duplicate field key " + key);
            writeField(out, *field, *attribute, type, key);
        }

        out << "    }" << (bases.empty() ? "" : ")") << ";\n"
            << "    static_assert(uniqueFieldNames(kFields), \"duplicate metadata field keys\");\n};\n\n";
        return true;
    }

    bool VisitFieldDecl(clang::FieldDecl* declaration)
    {
        if (annotation(*declaration, kField) && findHeader(*declaration) && !hasSchema(*declaration->getParent()))
            error(*declaration, "metadata fields require an annotated record");

        return true;
    }

    bool VisitEnumDecl(clang::EnumDecl* declaration)
    {
        const bool flags = annotation(*declaration, kFlags);
        if (!declaration->isThisDeclarationADefinition() || (!flags && !annotation(*declaration, kEnum)))
            return true;
        auto* header = findHeader(*declaration);
        if (!header)
            return true;
        if (!declaration->getIdentifier() || declaration->isInAnonymousNamespace() ||
            declaration->getDeclContext()->isFunctionOrMethod() || declaration->getDeclContext()->isDependentContext())
        {
            error(*declaration, "metadata enums must have a qualified name available outside their declaration");
            return true;
        }

        const auto type = "::" + declaration->getQualifiedNameAsString();
        llvm::raw_string_ostream out(header->declarations);
        out << "template <>\nstruct Enum<" << type << ">\n{\n"
            << "    static constexpr bool kFlags = " << (flags ? "true" : "false") << ";\n"
            << "    static constexpr std::array<EnumValue<" << type << ">, "
            << std::distance(declaration->enumerator_begin(), declaration->enumerator_end()) << "> kValues{{\n";

        std::set<StringRef> keys;
        for (const auto* enumerator : declaration->enumerators())
        {
            const auto* value = annotation(*enumerator, kEnumValue);
            if (!value)
            {
                error(*enumerator, "metadata enumerators require an explicit name");
                continue;
            }
            const auto key = keyArgument(*enumerator, *value, 0);
            if (!keys.insert(key).second)
                error(*enumerator, "duplicate enumerator key " + key);
            quoted(out << "        {" << type << "::" << enumerator->getName() << ", ", key) << "},\n";
        }

        out << "    }};\n};\n\n";
        return true;
    }

private:
    Header* findHeader(const clang::Decl& declaration)
    {
        const auto& sources = declaration.getASTContext().getSourceManager();
        const auto location = sources.getExpansionLoc(declaration.getLocation());
        if (location.isInvalid())
            return nullptr;
        const auto file = sources.getFileEntryRefForID(sources.getFileID(location));
        if (!file)
            return nullptr;

        const auto it = metadata.headers.find(realPath(file->getName()));
        return it != metadata.headers.end() ? &it->second : nullptr;
    }

    Metadata& metadata;
};

class Consumer : public clang::ASTConsumer
{
public:
    explicit Consumer(Metadata& metadata) : metadata(metadata) {}

    void HandleTranslationUnit(clang::ASTContext& context) override
    {
        Scan scan{metadata};
        scan.TraverseDecl(context.getTranslationUnitDecl());
    }

private:
    Metadata& metadata;
};

class Action : public clang::ASTFrontendAction
{
public:
    explicit Action(Metadata& metadata) : metadata(metadata) {}

    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(clang::CompilerInstance&, StringRef) override
    {
        return std::make_unique<Consumer>(metadata);
    }

private:
    Metadata& metadata;
};

class ActionFactory : public clang::tooling::FrontendActionFactory
{
public:
    explicit ActionFactory(Metadata& metadata) : metadata(metadata) {}

    std::unique_ptr<clang::FrontendAction> create() override { return std::make_unique<Action>(metadata); }

private:
    Metadata& metadata;
};

constexpr auto kPreamble = "// Generated by RenderLabMetadata.\n#pragma once\n\n#include \"core/Metadata.h\"\n";

bool writeOutput(StringRef path, StringRef content)
{
    const auto previous = llvm::MemoryBuffer::getFile(path);
    if (previous && (*previous)->getBuffer() == content)
        return true;

    auto failure = llvm::sys::fs::create_directories(llvm::sys::path::parent_path(path));
    if (!failure)
    {
        llvm::ToolOutputFile file(path, failure, llvm::sys::fs::OF_None);
        if (!failure)
        {
            file.os() << content;
            file.os().close();
            failure = file.os().error();
            if (!failure)
            {
                file.keep();
                return true;
            }
            file.os().clear_error();
        }
    }

    llvm::errs() << "Cannot write " << path << ": " << failure.message() << '\n';
    return false;
}

bool writeRegistry(const Metadata& metadata, StringRef path)
{
    std::set<StringRef> headers;
    for (const auto& [root, types] : metadata.registries)
        for (const auto& [id, registration] : types)
            headers.insert(registration.header);

    std::string content;
    llvm::raw_string_ostream out(content);
    out << kPreamble;
    for (auto file : headers)
    {
        file.consume_front("src/");
        quoted(out << "#include ", file) << '\n';
    }

    out << "\nnamespace metadata\n{\n";
    for (const auto& [root, types] : metadata.registries)
    {
        out << "template <>\nstruct RegisteredTypes<::" << root << ">\n{\n    using Types = TypeList<\n";
        for (auto entry = types.begin(); entry != types.end(); ++entry)
            out << "        " << entry->second.type << (std::next(entry) == types.end() ? ">;\n" : ",\n");
        out << "};\n\n";
    }
    out << "}\n";
    if (!writeOutput(path, content))
        return false;

    std::string implementation;
    llvm::raw_string_ostream source(implementation);
    source << "// Generated by RenderLabMetadata.\n#include \"core/TypeRegistry.h\"\n";
    quoted(source << "#include ", llvm::sys::path::filename(path)) << "\n\n";
    source << "template <class Base>\nTypeRegistry<Base>& TypeRegistry<Base>::get()\n"
              "{\n    static TypeRegistry registry;\n    return registry;\n}\n\n";
    for (const auto& [root, types] : metadata.registries)
        source << "template TypeRegistry<::" << root << ">& TypeRegistry<::" << root << ">::get();\n";

    llvm::SmallString<256> filename(path);
    llvm::sys::path::replace_extension(filename, ".cpp");
    return writeOutput(filename, implementation);
}
}

int main(int argc, char** argv)
{
    if (argc < 8)
    {
        llvm::errs() << "Usage: RenderLabMetadata <clang> <scan.cpp> <source root> <output root> <registry or -> "
                        "<headers...> -- <Clang arguments...>\n";
        return 1;
    }

    Metadata metadata;
    int next = 6;
    for (; next < argc && StringRef(argv[next]) != "--"; ++next)
    {
        const auto source = realPath(outputPath(argv[3], argv[next]));
        if (source.empty())
        {
            llvm::errs() << "Cannot locate metadata input " << argv[next] << '\n';
            return 1;
        }

        llvm::SmallString<256> filename(argv[next]);
        llvm::sys::path::replace_extension(filename, ".generated.h");
        metadata.headers.emplace(source, Header{argv[next], outputPath(argv[4], filename), {}});
    }
    if (next == argc || metadata.headers.empty())
    {
        llvm::errs() << "Metadata generation requires input headers and a Clang argument separator\n";
        return 1;
    }

    const std::vector<std::string> flags(argv + next + 1, argv + argc);
    const clang::tooling::FixedCompilationDatabase database(".", flags);
    clang::tooling::ClangTool tool(database, {argv[2]});
    tool.clearArgumentsAdjusters();
    tool.appendArgumentsAdjuster([driver = std::string{argv[1]}](const auto& arguments, StringRef)
    {
        auto command = arguments;
        command.front() = driver;
        return command;
    });

    ActionFactory factory(metadata);
    if (const auto result = tool.run(&factory); result != 0)
        return result;

    for (const auto& [source, header] : metadata.headers)
    {
        if (header.declarations.empty())
        {
            llvm::errs() << "No metadata declarations found in " << header.source << '\n';
            return 1;
        }
        const auto content = kPreamble + std::string{"\nnamespace metadata\n{\n"} + header.declarations + "}\n";
        if (!writeOutput(header.output, content))
            return 1;
    }

    const StringRef registry(argv[5]);
    return registry == "-" || writeRegistry(metadata, outputPath(argv[4], registry)) ? 0 : 1;
}
