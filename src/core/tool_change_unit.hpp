#pragma once
#include "core.hpp"

namespace horizon {
class ToolChangeUnit : public ToolBase {
public:
    ToolChangeUnit(Core *c, ToolID tid);
    ToolResponse begin(const ToolArgs &args) override;
    ToolResponse update(const ToolArgs &args) override;
    bool can_begin() override;

private:
};
} // namespace horizon
