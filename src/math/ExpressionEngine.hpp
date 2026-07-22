#pragma once
#include <string>
#include <vector>
#include <memory>

namespace mu { class Parser; }

class ExpressionEngine {
public:
    ExpressionEngine();
    ~ExpressionEngine();

    // Legacy convenience: define variables B1..Bn (one per layer). Retained for
    // backward compatibility; implemented in terms of setVariables().
    void setNumBands(int n);
    // Define named variables (e.g. "L1B1", "L2B3"). Each evaluate() input maps to
    // the variable at the same index, in the order given here.
    void setVariables(const std::vector<std::string>& names);
    bool setExpression(const std::string& expr);
    bool evaluate(const std::vector<const float*>& inputs,
                  float* output, int nPix);

    // Names of the currently-defined variables that the parsed expression actually
    // references (via muParser GetUsedVar). Empty if no valid expression.
    std::vector<std::string> usedVariables() const;

    const std::string& expression() const { return m_expr; }
    const std::string& errorMsg()   const { return m_error; }
    bool isValid() const { return m_valid; }

private:
    std::unique_ptr<mu::Parser> m_parser;
    std::vector<double>         m_vars;
    std::vector<std::string>    m_var_names;
    std::string                 m_expr;
    std::string                 m_error;
    bool                        m_valid{false};
    int                         m_n_bands{0};
};
