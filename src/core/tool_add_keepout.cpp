#include "tool_add_keepout.hpp"
#include "board/board_layers.hpp"
#include "core_board.hpp"
#include "core_package.hpp"
#include "imp/imp_interface.hpp"
#include "pool/part.hpp"
#include <iostream>

namespace horizon {

ToolAddKeepout::ToolAddKeepout(Core *c, ToolID tid) : ToolBase(c, tid)
{
}

Polygon *ToolAddKeepout::get_poly()
{
    Polygon *poly = nullptr;
    for (const auto &it : core.r->selection) {
        switch (it.type) {
        case ObjectType::POLYGON_ARC_CENTER:
        case ObjectType::POLYGON_EDGE:
        case ObjectType::POLYGON_VERTEX: {
            auto p = core.r->get_polygon(it.uuid);
            if (poly && poly != p) {
                return nullptr;
            }
            else {
                poly = p;
            }
        } break;
        default:;
        }
    }
    return poly;
}

bool ToolAddKeepout::can_begin()
{
    if (!core.r->has_object_type(ObjectType::KEEPOUT))
        return false;
    auto poly = get_poly();
    if (!poly)
        return false;
    switch (tool_id) {
    case ToolID::ADD_KEEPOUT:
        return poly->usage == nullptr;

    case ToolID::DELETE_KEEPOUT:
    case ToolID::EDIT_KEEPOUT:
        return poly->usage && poly->usage->get_type() == PolygonUsage::Type::KEEPOUT;

    default:
        return false;
    }
}

ToolResponse ToolAddKeepout::begin(const ToolArgs &args)
{
    auto poly = get_poly();
    Keepout *keepout = nullptr;
    if (tool_id == ToolID::EDIT_KEEPOUT) {
        keepout = dynamic_cast<Keepout *>(poly->usage.ptr);
    }
    else {
        keepout = core.r->insert_keepout(UUID::random());
        keepout->polygon = poly;
        poly->usage = keepout;
    }
    UUID keepout_uuid = keepout->uuid;
    bool r = imp->dialogs.edit_keepout(keepout, core.r);
    auto keepouts = core.r->get_keepouts();
    if (r) {
        if (std::count_if(keepouts.begin(), keepouts.end(), [keepout_uuid](auto x) { return x->uuid == keepout_uuid; })
            == 0) { // may have been deleted
            poly->usage = nullptr;
        }
        core.r->commit();
    }
    else {
        core.r->revert();
    }
    return ToolResponse::end();
}

ToolResponse ToolAddKeepout::update(const ToolArgs &args)
{
    return ToolResponse();
}
} // namespace horizon
