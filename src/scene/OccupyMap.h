// Port of alternativa.editor.scene.OccupyMap / Layers / EventJournal.
#pragma once
#include <map>
#include <string>
#include <tuple>
#include <unordered_set>
#include <vector>

#include "scene/Prop.h"

namespace ae {

// Insertion-ordered set of props (replacement for alternativa.types.Set).
class PropSet {
  public:
    bool add(const PropRef& p) {
        if (!p || has(p)) return false;
        items_.push_back(p);
        index_.insert(p.get());
        return true;
    }
    bool remove(const PropRef& p) { return remove(p.get()); }
    bool remove(const Prop* p) {
        if (!index_.erase(p)) return false;
        for (auto it = items_.begin(); it != items_.end(); ++it)
            if (it->get() == p) {
                items_.erase(it);
                break;
            }
        return true;
    }
    bool has(const PropRef& p) const { return p && index_.count(p.get()) > 0; }
    bool has(const Prop* p) const { return index_.count(p) > 0; }
    void clear() {
        items_.clear();
        index_.clear();
    }
    bool empty() const { return items_.empty(); }
    size_t size() const { return items_.size(); }
    PropRef peek() const { return items_.empty() ? nullptr : items_.front(); }
    const std::vector<PropRef>& items() const { return items_; }
    auto begin() const { return items_.begin(); }
    auto end() const { return items_.end(); }

  private:
    std::vector<PropRef> items_;
    std::unordered_set<const Prop*> index_;
};

class OccupyMap {
  public:
    void occupy(Prop& p);
    void free(Prop& p);
    void clear() { cells_.clear(); }
    bool isConflict(const Prop& p) const;
    bool isConflictGroup(const Prop& p) const;
    std::vector<Prop*> getConflictProps() const;

  private:
    using Key = std::tuple<double, double, double>;
    std::map<Key, std::vector<Prop*>> cells_;
    const std::vector<Prop*>* at(double x, double y, double z) const;
    template <typename F>
    static void forEachCell(const Prop& p, F fn);
};

struct Layer {
    bool visible = true;
    std::unordered_set<const Prop*> props;
};

class Layers {
  public:
    static const std::vector<std::string>& windowLayers();  // dm, tdm, dom, bonuses, special
    Layer& get(const std::string& name) { return layers_[name]; }
    void addProp(const std::string& layer, const Prop* p) { layers_[layer].props.insert(p); }
    void removeProp(const Prop* p, const std::string& layer = "");
    std::vector<Layer*> layersContaining(const Prop* p);
    void clear();

  private:
    std::map<std::string, Layer> layers_;
};

struct JournalItem {
    enum Op { ADD = 0, DELETE = 1, ROTATE = 2, MOVE = 3, COPY = 4, CHANGE_TEXTURE = 5 };
    Op op = ADD;
    PropSet props;
    Vec3 delta;              // MOVE: oldState (Point3D)
    bool clockwise = false;  // ROTATE: oldState (Boolean)
};

class EventJournal {
  public:
    void addEvent(JournalItem::Op op, const PropSet& props, Vec3 delta = {}, bool cw = false);
    void clear() {
        events_.clear();
        cancelEvents_.clear();
    }
    // Returns nullptr if empty. `discard` drops the item instead of pushing it to the redo list.
    JournalItem* undo(bool discard = false);
    JournalItem* redo();

  private:
    std::vector<std::unique_ptr<JournalItem>> events_, cancelEvents_;
    std::unique_ptr<JournalItem> discarded_;
};

}  // namespace ae
