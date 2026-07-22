#include "math/ExpressionEngine.hpp"
#include "util/Logger.hpp"

// The muParser package (vcpkg and conda-forge alike) is built without _UNICODE,
// exporting std::string symbols. MSVC defines _UNICODE globally which would make
// muParser headers declare wchar_t overloads instead — causing LNK2019 unresolved
// externals. Temporarily suppress the Unicode macros around the include so
// mu::string_type resolves to std::string in this translation unit, matching the
// library exports.
#ifdef _MSC_VER
#  pragma push_macro("_UNICODE")
#  pragma push_macro("UNICODE")
#  undef _UNICODE
#  undef UNICODE
#endif
#include <muParser.h>
#ifdef _MSC_VER
#  pragma pop_macro("UNICODE")
#  pragma pop_macro("_UNICODE")
#endif

#include <algorithm>

ExpressionEngine::ExpressionEngine()
    : m_parser(std::make_unique<mu::Parser>())
{}

ExpressionEngine::~ExpressionEngine() = default;

void ExpressionEngine::setNumBands(int n) {
    std::vector<std::string> names;
    names.reserve(static_cast<size_t>(std::max(0, n)));
    for (int i = 0; i < n; ++i)
        names.push_back("B" + std::to_string(i + 1));
    setVariables(names);
}

void ExpressionEngine::setVariables(const std::vector<std::string>& names) {
    m_var_names = names;
    m_n_bands   = static_cast<int>(names.size());
    m_vars.assign(names.size(), 0.0);
    m_parser->ClearVar();
    for (size_t i = 0; i < names.size(); ++i)
        m_parser->DefineVar(names[i], &m_vars[i]);
    if (!m_expr.empty()) setExpression(m_expr);
}

std::vector<std::string> ExpressionEngine::usedVariables() const {
    std::vector<std::string> used;
    if (!m_valid) return used;
    try {
        const mu::varmap_type& vars = m_parser->GetUsedVar();
        // Preserve the order in which variables were defined (m_var_names).
        for (const auto& name : m_var_names)
            if (vars.find(name) != vars.end())
                used.push_back(name);
    } catch (const mu::Parser::exception_type& e) {
        FV_WARN("ExpressionEngine::usedVariables error: {}", e.GetMsg());
    }
    return used;
}

bool ExpressionEngine::setExpression(const std::string& expr) {
    m_expr  = expr;
    m_valid = false;
    m_error.clear();
    try {
        m_parser->SetExpr(expr);
        for (auto& v : m_vars) v = 0.0;
        m_parser->Eval();
        m_valid = true;
    } catch (const mu::Parser::exception_type& e) {
        m_error = e.GetMsg();
        FV_WARN("ExpressionEngine: parse error: {}", m_error);
    }
    return m_valid;
}

bool ExpressionEngine::evaluate(const std::vector<const float*>& inputs,
                                  float* output, int nPix) {
    if (!m_valid || static_cast<int>(inputs.size()) < m_n_bands) return false;

    try {
        for (int i = 0; i < nPix; ++i) {
            for (int b = 0; b < m_n_bands; ++b)
                m_vars[static_cast<size_t>(b)] = static_cast<double>(inputs[static_cast<size_t>(b)][i]);
            output[i] = static_cast<float>(m_parser->Eval());
        }
    } catch (const mu::Parser::exception_type& e) {
        m_error = e.GetMsg();
        FV_ERROR("ExpressionEngine::evaluate error: {}", m_error);
        return false;
    }
    return true;
}
