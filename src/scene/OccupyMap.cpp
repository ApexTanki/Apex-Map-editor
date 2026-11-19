#include "scene/OccupyMap.h"

#include <algorithm>

#include "scene/Grid.h"

namespace ae {

template <typename F>
void OccupyMap::forEachCell(const Prop& p, F fn) {
    const double x0 = p.distancesX.x + p.x, x1 = p.distancesX.y + p.x;
    const double y0 = p.distancesY.x + p.y, y1 = p.distancesY.y + p.y;
    const double z0 = p.distancesZ.x + p.z, z1 = p.distancesZ.y + p.z;
    for (double cx = x0; cx < x1; cx += grid::hBase)
        for (double cy = y0; cy < y1; cy += grid::hBase)
            for (double cz = z0; cz < z1; cz += grid::vBase)
                if (!fn(cx, cy, cz)) return;
}

void OccupyMap::occupy(Prop& p) {
    if (!p.free) return;
    forEachCell(p, [&](double x, double y, double z) {
        cells_[{x, y, z}].push_back(&p);
        return true;
    });
    p.free = false;
}

void OccupyMap::free(Prop& p) {
    if (p.free) return;
    forEachCell(p, [&](double x, double y, double z) {
        auto it = cells_.find({x, y, z});
        if (it != cells_.end()) {
            auto& v = it->second;
            auto f = std::find(v.begin(), v.end(), &p);
            if (f != v.end()) v.erase(f);
            if (v.empty()) cells_.erase(it);
        }
        return true;
    });
    p.free = true;
}

const std::vector<Prop*>* OccupyMap::at(double x, double y, double z) const {
    auto it = cells_.find({x, y, z});
    return it == cells_.end() ? nullptr : &it->second;
}

bool OccupyMap::isConflict(const Prop& p) const {
    bool conflict = false;
    forEachCell(p, [&](double x, double y, double z) {
        const auto* v = at(x, y, z);
        if (v && (std::find(v->begin(), v->end(), &p) == v->end() || v->size() > 1)) {
            conflict = true;
            return false;
        }
        return true;
    });
    return conflict;
}

bool OccupyMap::isConflictGroup(const Prop& p) const {
    bool conflict = false;
    forEachCell(p, [&](double x, double y, double z) {
        const auto* v = at(x, y, z);
        if (v)
            for (Prop* o : *v)
                if (o != &p && o->groupName == p.groupName) {
                    conflict = true;
                    return false;
                }
        return true;
    });
    return conflict;
}

std::vector<Prop*> OccupyMap::getConflictProps() const {
    std::vector<Prop*> out;
    std::unordered_set<Prop*> seen;
    for (auto& [key, v] : cells_)
        if (v.size() > 1)
            for (Prop* p : v)
                if (seen.insert(p).second) out.push_back(p);
    return out;
}

// ---------------------------------------------------------------------------------------------

const std::vector<std::string>& Layers::windowLayers() {
    static const std::vector<std::string> v = {"dm", "tdm", "dom", "bonuses", "special"};
    return v;
}

void Layers::removeProp(const Prop* p, const std::string& layer) {
    if (!layer.empty()) {
        layers_[layer].props.erase(p);
        return;
    }
    for (auto& [n, l] : layers_) l.props.erase(p);
}

std::vector<Layer*> Layers::layersContaining(const Prop* p) {
    std::vector<Layer*> out;
    for (auto& [n, l] : layers_)
        if (l.props.count(p)) out.push_back(&l);
    return out;
}

void Layers::clear() {
    for (auto& [n, l] : layers_) l.props.clear();
}

// ---------------------------------------------------------------------------------------------

void EventJournal::addEvent(JournalItem::Op op, const PropSet& props, Vec3 delta, bool cw) {
    auto item = std::make_unique<JournalItem>();
    item->op = op;
    item->props = props;
    item->delta = delta;
    item->clockwise = cw;
    events_.push_back(std::move(item));
    cancelEvents_.clear();
}

JournalItem* EventJournal::undo(bool discard) {
    if (events_.empty()) return nullptr;
    std::unique_ptr<JournalItem> item = std::move(events_.back());
    events_.pop_back();
    JournalItem* raw = item.get();
    if (discard)
        discarded_ = std::move(item);
    else
        cancelEvents_.push_back(std::move(item));
    return raw;
}

JournalItem* EventJournal::redo() {
    if (cancelEvents_.empty()) return nullptr;
    std::unique_ptr<JournalItem> item = std::move(cancelEvents_.back());
    cancelEvents_.pop_back();
    JournalItem* raw = item.get();
    events_.push_back(std::move(item));
    return raw;
}

}  // namespace ae
