#include "editor_window.hpp"
#include "unit_editor.hpp"
#include "entity_editor.hpp"
#include "part_editor.hpp"
#include "pool/unit.hpp"
#include "pool/entity.hpp"
#include "pool/part.hpp"
#include "util/util.hpp"
#include "util/str_util.hpp"
#include "pool/pool.hpp"
#include "nlohmann/json.hpp"

namespace horizon {

EditorWindowStore::EditorWindowStore(const std::string &fn) : filename(fn)
{
}

void EditorWindowStore::save()
{
    save_as(filename);
}

class UnitStore : public EditorWindowStore {
public:
    UnitStore(const std::string &fn)
        : EditorWindowStore(fn), unit(filename.size() ? Unit::new_from_file(filename) : Unit(UUID::random()))
    {
    }
    void save_as(const std::string &fn)
    {
        save_json_to_file(fn, unit.serialize());
        filename = fn;
    }
    std::string get_name() const override
    {
        return unit.name;
    }
    Unit unit;
};

class EntityStore : public EditorWindowStore {
public:
    EntityStore(const std::string &fn, class Pool *pool)
        : EditorWindowStore(fn),
          entity(filename.size() ? Entity::new_from_file(filename, *pool) : Entity(UUID::random()))
    {
    }
    void save_as(const std::string &fn)
    {
        save_json_to_file(fn, entity.serialize());
        filename = fn;
    }
    std::string get_name() const override
    {
        return entity.name;
    }
    Entity entity;
};

class PartStore : public EditorWindowStore {
public:
    PartStore(const std::string &fn, class Pool *pool)
        : EditorWindowStore(fn), part(filename.size() ? Part::new_from_file(filename, *pool) : Part(UUID::random()))
    {
    }
    void save_as(const std::string &fn)
    {
        save_json_to_file(fn, part.serialize());
        filename = fn;
    }
    std::string get_name() const override
    {
        return part.get_MPN();
    }
    Part part;
};

EditorWindow::EditorWindow(ObjectType ty, const std::string &filename, Pool *p, class PoolParametric *pp,
                           bool read_only)
    : Gtk::Window(), type(ty), pool(p), pool_parametric(pp),
      state_store(this, "pool-editor-win-" + std::to_string(static_cast<int>(type)))
{
    set_type_hint(Gdk::WINDOW_TYPE_HINT_DIALOG);
    auto hb = Gtk::manage(new Gtk::HeaderBar());
    set_titlebar(*hb);

    std::string save_label;
    if (filename.size()) {
        save_label = "Save";
    }
    else {
        save_label = "Save as..";
    }
    save_button = Gtk::manage(new Gtk::Button(save_label));
    if (read_only)
        save_button->set_sensitive(false);
    hb->pack_start(*save_button);

    hb->show_all();
    hb->set_show_close_button(true);


    Gtk::Widget *editor = nullptr;
    switch (type) {
    case ObjectType::UNIT: {
        auto st = new UnitStore(filename);
        store.reset(st);
        auto ed = UnitEditor::create(&st->unit, pool);
        editor = ed;
        iface = ed;
        hb->set_title("Unit Editor");
    } break;
    case ObjectType::ENTITY: {
        auto st = new EntityStore(filename, pool);
        store.reset(st);
        auto ed = EntityEditor::create(&st->entity, pool);
        editor = ed;
        iface = ed;
        hb->set_title("Entity Editor");
    } break;
    case ObjectType::PART: {
        auto st = new PartStore(filename, pool);
        store.reset(st);
        auto ed = PartEditor::create(&st->part, pool, pool_parametric);
        editor = ed;
        iface = ed;
        hb->set_title("Part Editor");
    } break;
    default:;
    }
    editor->show();
    add(*editor);
    editor->unreference();

    if (iface) {
        iface->signal_goto().connect([this](ObjectType t, UUID uu) { s_signal_goto.emit(t, uu); });
    }

    if (!state_store.get_default_set())
        set_default_size(-1, 600);

    signal_delete_event().connect([this, read_only](GdkEventAny *ev) {
        if (iface && iface->get_needs_save()) {
            if (!read_only) { // not read only
                Gtk::MessageDialog md(*this, "Save changes before closing?", false /* use_markup */,
                                      Gtk::MESSAGE_QUESTION, Gtk::BUTTONS_NONE);
                md.set_secondary_text(
                        "If you don't save, all your changes will be permanently "
                        "lost.");
                md.add_button("Close without Saving", 1);
                md.add_button("Cancel", Gtk::RESPONSE_CANCEL);
                md.add_button("Save", 2);
                switch (md.run()) {
                case 1:
                    return false; // close

                case 2:
                    save();
                    return false; // close

                default:
                    return true; // keep window open
                }
                return false;
            }
            else {
                Gtk::MessageDialog md(*this, "Document is read only", false /* use_markup */, Gtk::MESSAGE_QUESTION,
                                      Gtk::BUTTONS_NONE);
                md.set_secondary_text("You won't be able to save your changes");
                md.add_button("Close without Saving", 1);
                md.add_button("Cancel", Gtk::RESPONSE_CANCEL);
                switch (md.run()) {
                case 1:
                    return false; // close

                default:
                    return true; // keep window open
                }
                return false;
            }
        }
        return false;
    });

    save_button->signal_clicked().connect(sigc::mem_fun(this, &EditorWindow::save));
}

void EditorWindow::force_close()
{
    hide();
}

bool EditorWindow::get_needs_save()
{
    return iface->get_needs_save();
}

void EditorWindow::save()
{
    if (iface)
        iface->save();

    if (store->filename.size()) {
        store->save();
    }
    else {
        GtkFileChooserNative *native = gtk_file_chooser_native_new("Save", GTK_WINDOW(gobj()),
                                                                   GTK_FILE_CHOOSER_ACTION_SAVE, "_Save", "_Cancel");
        auto chooser = Glib::wrap(GTK_FILE_CHOOSER(native));
        chooser->set_do_overwrite_confirmation(true);
        chooser->set_current_name(store->get_name() + ".json");
        switch (type) {
        case ObjectType::UNIT:
            chooser->set_current_folder(Glib::build_filename(pool->get_base_path(), "units"));
            break;
        case ObjectType::ENTITY:
            chooser->set_current_folder(Glib::build_filename(pool->get_base_path(), "entities"));
            break;
        default:;
        }

        if (gtk_native_dialog_run(GTK_NATIVE_DIALOG(native)) == GTK_RESPONSE_ACCEPT) {
            std::string fn = fix_filename(chooser->get_filename());
            store->save_as(fn);
            save_button->set_label("Save");
        }
    }
    need_update = true;
}

std::string EditorWindow::fix_filename(std::string s)
{
    trim(s);
    if (!endswith(s, ".json")) {
        s += ".json";
    }
    return s;
}

void EditorWindow::reload()
{
    if (iface) {
        iface->reload();
    }
}

bool EditorWindow::get_need_update()
{
    return need_update;
}
} // namespace horizon
