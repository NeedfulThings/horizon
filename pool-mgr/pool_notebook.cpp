#include "pool_notebook.hpp"
#include "widgets/pool_browser_unit.hpp"
#include "widgets/pool_browser_symbol.hpp"
#include "widgets/pool_browser_entity.hpp"
#include "widgets/pool_browser_padstack.hpp"
#include "widgets/pool_browser_package.hpp"
#include "widgets/pool_browser_part.hpp"
#include "canvas/canvas.hpp"
#include "util/util.hpp"
#include "pool-update/pool-update.hpp"
#include <zmq.hpp>
#include "editor_window.hpp"
#include "create_part_dialog.hpp"
#include "part_wizard/part_wizard.hpp"

namespace horizon {

	PoolManagerProcess::PoolManagerProcess(PoolManagerProcess::Type ty,  const std::vector<std::string>& args, const std::vector<std::string>& ienv, Pool *pool): type(ty) {
		std::cout << "create proc" << std::endl;
		if(type == PoolManagerProcess::Type::IMP_SYMBOL || type == PoolManagerProcess::Type::IMP_PADSTACK || type == PoolManagerProcess::Type::IMP_PACKAGE) { //imp
			std::vector<std::string> argv;
			std::vector<std::string> env = ienv;
			auto envs = Glib::listenv();
			for(const auto &it: envs) {
				env.push_back(it+"="+Glib::getenv(it));
			}
			auto exe_dir = get_exe_dir();
			auto imp_exe = Glib::build_filename(exe_dir, "horizon-imp");
			argv.push_back(imp_exe);
			switch(type) {
				case PoolManagerProcess::Type::IMP_SYMBOL :
					argv.push_back("-y");
					argv.insert(argv.end(), args.begin(), args.end());
				break;
				case PoolManagerProcess::Type::IMP_PADSTACK :
					argv.push_back("-a");
					argv.insert(argv.end(), args.begin(), args.end());
				break;
				case PoolManagerProcess::Type::IMP_PACKAGE :
					argv.push_back("-k");
					argv.insert(argv.end(), args.begin(), args.end());
				break;
				default:;
			}
			proc = std::make_unique<EditorProcess>(argv, env);
			proc->signal_exited().connect([this](auto rc){s_signal_exited.emit(rc);});
		}
		else {
			switch(type) {
				case PoolManagerProcess::Type::UNIT :
					win = new EditorWindow(ObjectType::UNIT, args.at(0), pool);
				break;
				case PoolManagerProcess::Type::ENTITY :
					win = new EditorWindow(ObjectType::ENTITY, args.at(0), pool);
				break;
				case PoolManagerProcess::Type::PART :
					win = new EditorWindow(ObjectType::PART, args.at(0), pool);
				break;
				default:;
			}
			win->present();

			win->signal_hide().connect([this] {
				delete win;
				s_signal_exited.emit(0);
			});
		}
	}

	void PoolManagerProcess::reload() {
		if(auto w = dynamic_cast<EditorWindow*>(win)) {
			w->reload();
		}
	}

	void PoolNotebook::spawn(PoolManagerProcess::Type type, const std::vector<std::string> &args) {
		if(processes.count(args.at(0)) == 0) { //need to launch imp
			std::vector<std::string> env = {"HORIZON_POOL="+base_path};
			std::string filename = args.at(0);
			auto &proc = processes.emplace(std::piecewise_construct, std::forward_as_tuple(filename),
					std::forward_as_tuple(type, args, env, &pool)).first->second;

			proc.signal_exited().connect([filename, this](int status) {
				std::cout << "exit stat " << status << std::endl;
				/*if(status != 0) {
					view_project.info_bar_label->set_text("Editor for '"+filename+"' exited with status "+std::to_string(status));
					view_project.info_bar->show();

					//ugly workaround for making the info bar appear
					auto parent = dynamic_cast<Gtk::Box*>(view_project.info_bar->get_parent());
					parent->child_property_padding(*view_project.info_bar) = 1;
					parent->child_property_padding(*view_project.info_bar) = 0;
				}*/
				processes.erase(filename);
				pool_update();
			});
		}
		else { //present imp
			//auto &proc = processes.at(args.at(0));
			//auto pid = proc.proc->get_pid();
			//app->send_json(pid, {{"op", "present"}});
		}
	}

	PoolNotebook::PoolNotebook(const std::string &bp): Gtk::Notebook(), base_path(bp), pool(bp) {\
		{
			auto br = Gtk::manage(new PoolBrowserUnit(&pool));
			br->signal_activated().connect([this, br] {
				auto uu = br->get_selected();
				auto path = pool.get_filename(ObjectType::UNIT, uu);
				spawn(PoolManagerProcess::Type::UNIT, {path});
			});

			br->show();
			browsers.insert(br);

			auto box = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 0));
			auto bbox = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 8));
			bbox->set_margin_bottom(8);
			bbox->set_margin_top(8);
			bbox->set_margin_start(8);
			bbox->set_margin_end(8);

			{
				auto bu = Gtk::manage(new Gtk::Button("Create Unit"));
				bbox->pack_start(*bu, false, false,0);
				bu->signal_clicked().connect([this]{
					spawn(PoolManagerProcess::Type::UNIT, {""});
				});
			}
			{
				auto bu = Gtk::manage(new Gtk::Button("Edit Unit"));
				bbox->pack_start(*bu, false, false,0);
				bu->signal_clicked().connect([this, br]{
					auto uu = br->get_selected();
					if(!uu)
						return;
					auto path = pool.get_filename(ObjectType::UNIT, uu);
					spawn(PoolManagerProcess::Type::UNIT, {path});
				});
			}
			{
				auto bu = Gtk::manage(new Gtk::Button("Create Symbol for Unit"));
				bbox->pack_start(*bu, false, false,0);
				bu->signal_clicked().connect([this, br]{
					if(!br->get_selected())
						return;
					auto top = dynamic_cast<Gtk::Window*>(get_ancestor(GTK_TYPE_WINDOW));
					Gtk::FileChooserDialog fc(*top, "Save Symbol", Gtk::FILE_CHOOSER_ACTION_SAVE);
					fc.set_do_overwrite_confirmation(true);
					fc.set_current_name("symbol.json");
					fc.set_current_folder(Glib::build_filename(base_path, "symbols"));
					fc.add_button("_Cancel", Gtk::RESPONSE_CANCEL);
					fc.add_button("_Save", Gtk::RESPONSE_ACCEPT);
					if(fc.run()==Gtk::RESPONSE_ACCEPT) {
						std::string fn = fc.get_filename();
						Symbol sym(horizon::UUID::random());
						auto unit = pool.get_unit(br->get_selected());
						sym.name = unit->name;
						sym.unit = unit;
						save_json_to_file(fn, sym.serialize());
						spawn(PoolManagerProcess::Type::IMP_SYMBOL, {fn});
					}
				});
			}
			{
				auto bu = Gtk::manage(new Gtk::Button("Create Entity for Unit"));
				bbox->pack_start(*bu, false, false,0);
				bu->signal_clicked().connect([this, br]{
					if(!br->get_selected())
						return;
					auto top = dynamic_cast<Gtk::Window*>(get_ancestor(GTK_TYPE_WINDOW));
					Gtk::FileChooserDialog fc(*top, "Save entity", Gtk::FILE_CHOOSER_ACTION_SAVE);
					fc.set_do_overwrite_confirmation(true);
					fc.set_current_folder(Glib::build_filename(base_path, "entities"));
					fc.set_current_name("entity.json");
					fc.add_button("_Cancel", Gtk::RESPONSE_CANCEL);
					fc.add_button("_Save", Gtk::RESPONSE_ACCEPT);
					if(fc.run()==Gtk::RESPONSE_ACCEPT) {
						std::string fn = fc.get_filename();
						Entity entity(horizon::UUID::random());
						auto unit = pool.get_unit(br->get_selected());
						entity.name = unit->name;
						auto uu = UUID::random();
						auto gate = &entity.gates.emplace(uu, uu).first->second;
						gate->unit = unit;
						gate->name = "Main";

						save_json_to_file(fn, entity.serialize());
						spawn(PoolManagerProcess::Type::ENTITY, {fn});
					}
				});
			}

			bbox->show_all();

			box->pack_start(*bbox, false, false, 0);

			auto sep = Gtk::manage(new Gtk::Separator(Gtk::ORIENTATION_HORIZONTAL));
			sep->show();
			box->pack_start(*sep, false, false, 0);
			box->pack_start(*br, true, true, 0);
			box->show();

			append_page(*box, "Units");
		}
		{
			auto br = Gtk::manage(new PoolBrowserSymbol(&pool));
			browsers.insert(br);
			br->show();
			auto paned = Gtk::manage(new Gtk::Paned(Gtk::ORIENTATION_HORIZONTAL));
			paned->add1(*br);
			auto canvas = Gtk::manage(new CanvasGL());
			canvas->set_selection_allowed(false);
			paned->add2(*canvas);
			paned->show_all();
			br->signal_selected().connect([this, br, canvas]{
				auto sel = br->get_selected();
				if(!sel) {
					canvas->clear();
					return;
				}
				Symbol sym = *pool.get_symbol(sel);
				for(const auto &la: sym.get_layers()) {
					canvas->set_layer_display(la.first, LayerDisplay(true, LayerDisplay::Mode::FILL, la.second.color));
				}
				sym.expand();
				canvas->update(sym);
				auto bb = sym.get_bbox();
				int64_t pad = 1_mm;
				bb.first.x -= pad;
				bb.first.y -= pad;

				bb.second.x += pad;
				bb.second.y += pad;
				canvas->zoom_to_bbox(bb.first, bb.second);
			});
			br->signal_activated().connect([this, br] {
				auto uu = br->get_selected();
				auto path = pool.get_filename(ObjectType::SYMBOL, uu);
				spawn(PoolManagerProcess::Type::IMP_SYMBOL, {path});
			});

			auto box = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 0));
			auto bbox = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 8));
			bbox->set_margin_bottom(8);
			bbox->set_margin_top(8);
			bbox->set_margin_start(8);
			bbox->set_margin_end(8);

			{
				auto bu = Gtk::manage(new Gtk::Button("Edit Symbol"));
				bbox->pack_start(*bu, false, false,0);
				bu->signal_clicked().connect([this, br]{
					auto uu = br->get_selected();
					if(!uu)
						return;
					auto path = pool.get_filename(ObjectType::SYMBOL, uu);
					spawn(PoolManagerProcess::Type::IMP_SYMBOL, {path});
				});
			}

			bbox->show_all();

			box->pack_start(*bbox, false, false, 0);

			auto sep = Gtk::manage(new Gtk::Separator(Gtk::ORIENTATION_HORIZONTAL));
			sep->show();
			box->pack_start(*sep, false, false, 0);
			box->pack_start(*paned, true, true, 0);
			box->show();

			append_page(*box, "Symbols");
		}


		{
			auto br = Gtk::manage(new PoolBrowserEntity(&pool));
			br->signal_activated().connect([this, br] {
				auto uu = br->get_selected();
				if(!uu)
						return;
				auto path = pool.get_filename(ObjectType::ENTITY, uu);
				spawn(PoolManagerProcess::Type::ENTITY, {path});
			});

			br->show();
			browsers.insert(br);

			auto box = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 0));
			auto bbox = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 8));
			bbox->set_margin_bottom(8);
			bbox->set_margin_top(8);
			bbox->set_margin_start(8);
			bbox->set_margin_end(8);

			{
				auto bu = Gtk::manage(new Gtk::Button("Create Entity"));
				bbox->pack_start(*bu, false, false,0);
				bu->signal_clicked().connect([this]{
					spawn(PoolManagerProcess::Type::ENTITY, {""});
				});
			}
			{
				auto bu = Gtk::manage(new Gtk::Button("Edit Entity"));
				bbox->pack_start(*bu, false, false,0);
				bu->signal_clicked().connect([this, br]{
					auto uu = br->get_selected();
					if(!uu)
						return;
					auto path = pool.get_filename(ObjectType::ENTITY, uu);
					spawn(PoolManagerProcess::Type::ENTITY, {path});
				});
			}

			bbox->show_all();

			box->pack_start(*bbox, false, false, 0);

			auto sep = Gtk::manage(new Gtk::Separator(Gtk::ORIENTATION_HORIZONTAL));
			sep->show();
			box->pack_start(*sep, false, false, 0);
			box->pack_start(*br, true, true, 0);
			box->show();

			append_page(*box, "Entities");
		}

		{
			auto br = Gtk::manage(new PoolBrowserPadstack(&pool));
			br->signal_activated().connect([this, br] {
				auto uu = br->get_selected();
				auto path = pool.get_filename(ObjectType::PADSTACK, uu);
				spawn(PoolManagerProcess::Type::IMP_PADSTACK, {path});
			});

			br->show();
			browsers.insert(br);

			auto paned = Gtk::manage(new Gtk::Paned(Gtk::ORIENTATION_HORIZONTAL));
			paned->add1(*br);
			auto canvas = Gtk::manage(new CanvasGL());
			canvas->set_selection_allowed(false);
			paned->add2(*canvas);
			paned->show_all();

			br->signal_selected().connect([this, br, canvas]{
				auto sel = br->get_selected();
				if(!sel) {
					canvas->clear();
					return;
				}
				Padstack ps = *pool.get_padstack(sel);
				for(const auto &la: ps.get_layers()) {
					canvas->set_layer_display(la.first, LayerDisplay(true, LayerDisplay::Mode::FILL, la.second.color));
				}
				canvas->property_layer_opacity() = 75;
				canvas->update(ps);
				auto bb = ps.get_bbox();
				int64_t pad = .1_mm;
				bb.first.x -= pad;
				bb.first.y -= pad;

				bb.second.x += pad;
				bb.second.y += pad;
				canvas->zoom_to_bbox(bb.first, bb.second);
			});


			auto box = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 0));
			auto bbox = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 8));
			bbox->set_margin_bottom(8);
			bbox->set_margin_top(8);
			bbox->set_margin_start(8);
			bbox->set_margin_end(8);

			{
				auto bu = Gtk::manage(new Gtk::Button("Create Padstack"));
				bbox->pack_start(*bu, false, false,0);
				bu->signal_clicked().connect([this]{
					auto top = dynamic_cast<Gtk::Window*>(get_ancestor(GTK_TYPE_WINDOW));
					Gtk::FileChooserDialog fc(*top, "Save Padstack", Gtk::FILE_CHOOSER_ACTION_SAVE);
					fc.set_do_overwrite_confirmation(true);
					fc.set_current_name("padstack.json");
					fc.set_current_folder(Glib::build_filename(base_path, "padstacks"));
					fc.add_button("_Cancel", Gtk::RESPONSE_CANCEL);
					fc.add_button("_Save", Gtk::RESPONSE_ACCEPT);
					if(fc.run()==Gtk::RESPONSE_ACCEPT) {
						std::string fn = fc.get_filename();
						Padstack ps(horizon::UUID::random());
						save_json_to_file(fn, ps.serialize());
						spawn(PoolManagerProcess::Type::IMP_PADSTACK, {fn});
					}
				});
			}
			{
				auto bu = Gtk::manage(new Gtk::Button("Edit Padstack"));
				bbox->pack_start(*bu, false, false,0);
				bu->signal_clicked().connect([this, br]{
					auto uu = br->get_selected();
					if(!uu)
						return;
					auto path = pool.get_filename(ObjectType::PADSTACK, uu);
					spawn(PoolManagerProcess::Type::IMP_PADSTACK, {path});
				});
			}

			bbox->show_all();

			box->pack_start(*bbox, false, false, 0);

			auto sep = Gtk::manage(new Gtk::Separator(Gtk::ORIENTATION_HORIZONTAL));
			sep->show();
			box->pack_start(*sep, false, false, 0);
			box->pack_start(*paned, true, true, 0);
			box->show();

			append_page(*box, "Padstacks");
		}

		{
			auto br = Gtk::manage(new PoolBrowserPackage(&pool));
			br->signal_activated().connect([this, br] {
				auto uu = br->get_selected();
				auto path = pool.get_filename(ObjectType::PACKAGE, uu);
				spawn(PoolManagerProcess::Type::IMP_PACKAGE, {path});
			});

			br->show();
			browsers.insert(br);

			auto paned = Gtk::manage(new Gtk::Paned(Gtk::ORIENTATION_HORIZONTAL));
			paned->add1(*br);
			auto canvas = Gtk::manage(new CanvasGL());
			canvas->set_selection_allowed(false);
			paned->add2(*canvas);
			paned->show_all();

			br->signal_selected().connect([this, br, canvas]{
				auto sel = br->get_selected();
				if(!sel) {
					canvas->clear();
					return;
				}
				Package pkg = *pool.get_package(sel);
				for(const auto &la: pkg.get_layers()) {
					canvas->set_layer_display(la.first, LayerDisplay(true, LayerDisplay::Mode::OUTLINE, la.second.color));
				}
				pkg.apply_parameter_set({});
				canvas->property_layer_opacity() = 75;
				canvas->update(pkg);
				auto bb = pkg.get_bbox();
				int64_t pad = 1_mm;
				bb.first.x -= pad;
				bb.first.y -= pad;

				bb.second.x += pad;
				bb.second.y += pad;
				canvas->zoom_to_bbox(bb.first, bb.second);
			});


			auto box = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 0));
			auto bbox = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 8));
			bbox->set_margin_bottom(8);
			bbox->set_margin_top(8);
			bbox->set_margin_start(8);
			bbox->set_margin_end(8);

			{
				auto bu = Gtk::manage(new Gtk::Button("Create Package"));
				bbox->pack_start(*bu, false, false,0);
				bu->signal_clicked().connect([this]{
					auto top = dynamic_cast<Gtk::Window*>(get_ancestor(GTK_TYPE_WINDOW));
					Gtk::FileChooserDialog fc(*top, "Save Package", Gtk::FILE_CHOOSER_ACTION_CREATE_FOLDER);
					fc.set_do_overwrite_confirmation(true);
					fc.set_current_name("package");
					fc.set_current_folder(Glib::build_filename(base_path, "packages"));
					fc.add_button("_Cancel", Gtk::RESPONSE_CANCEL);
					fc.add_button("_Save", Gtk::RESPONSE_ACCEPT);
					if(fc.run()==Gtk::RESPONSE_ACCEPT) {
						std::string fn = fc.get_filename();
						auto fi = Gio::File::create_for_path(Glib::build_filename(fn, "padstacks"));
						fi->make_directory_with_parents();
						Package pkg(horizon::UUID::random());
						auto pkg_filename = Glib::build_filename(fn, "package.json");
						save_json_to_file(pkg_filename, pkg.serialize());
						spawn(PoolManagerProcess::Type::IMP_PACKAGE, {pkg_filename});
					}
				});
			}
			{
				auto bu = Gtk::manage(new Gtk::Button("Edit Package"));
				bbox->pack_start(*bu, false, false,0);
				bu->signal_clicked().connect([this, br]{
					auto uu = br->get_selected();
					if(!uu)
						return;
					auto path = pool.get_filename(ObjectType::PACKAGE, uu);
					spawn(PoolManagerProcess::Type::IMP_PACKAGE, {path});
				});
			}
			{
				auto bu = Gtk::manage(new Gtk::Button("Create Padstack for Package"));
				bbox->pack_start(*bu, false, false,0);
				bu->signal_clicked().connect([this, br]{
					if(!br->get_selected())
						return;
					auto top = dynamic_cast<Gtk::Window*>(get_ancestor(GTK_TYPE_WINDOW));
					Gtk::FileChooserDialog fc(*top, "Save Padstack", Gtk::FILE_CHOOSER_ACTION_SAVE);
					fc.set_do_overwrite_confirmation(true);
					auto pkg_filename = pool.get_filename(ObjectType::PACKAGE, br->get_selected());
					auto pkg_dir = Glib::path_get_dirname(pkg_filename);
					fc.set_current_folder(Glib::build_filename(pkg_dir, "padstacks"));
					fc.set_current_name("padstack.json");
					fc.add_button("_Cancel", Gtk::RESPONSE_CANCEL);
					fc.add_button("_Save", Gtk::RESPONSE_ACCEPT);
					if(fc.run()==Gtk::RESPONSE_ACCEPT) {
						std::string fn = fc.get_filename();
						Padstack ps(horizon::UUID::random());
						ps.name = "Pad";
						save_json_to_file(fn, ps.serialize());
						spawn(PoolManagerProcess::Type::IMP_PADSTACK, {fn});
					}
				});
			}
			{
				auto bu = Gtk::manage(new Gtk::Button("Part wizard..."));
				bbox->pack_start(*bu, false, false,0);
				bu->signal_clicked().connect([this, br]{
					if(!br->get_selected())
						return;
					auto top = dynamic_cast<Gtk::Window*>(get_ancestor(GTK_TYPE_WINDOW));
					if(!part_wizard) {
						auto pkg = pool.get_package(br->get_selected());
						part_wizard = PartWizard::create(pkg, base_path, &pool);
						part_wizard->present();
						part_wizard->signal_hide().connect([this]{
							if(part_wizard->get_has_finished()) {
								pool_update();
							}
							delete part_wizard;
							part_wizard = nullptr;
						});
					}
					else {
						part_wizard->present();
					}

				});
			}

			bbox->show_all();

			box->pack_start(*bbox, false, false, 0);

			auto sep = Gtk::manage(new Gtk::Separator(Gtk::ORIENTATION_HORIZONTAL));
			sep->show();
			box->pack_start(*sep, false, false, 0);
			box->pack_start(*paned, true, true, 0);
			box->show();

			append_page(*box, "Packages");
		}

		{
			auto br = Gtk::manage(new PoolBrowserPart(&pool));
			br->signal_activated().connect([this, br] {
				auto uu = br->get_selected();
				if(!uu)
						return;
				auto path = pool.get_filename(ObjectType::PART, uu);
				spawn(PoolManagerProcess::Type::PART, {path});
			});

			br->show();
			browsers.insert(br);

			auto box = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 0));
			auto bbox = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 8));
			bbox->set_margin_bottom(8);
			bbox->set_margin_top(8);
			bbox->set_margin_start(8);
			bbox->set_margin_end(8);

			{
				auto bu = Gtk::manage(new Gtk::Button("Create Part"));
				bbox->pack_start(*bu, false, false,0);
				bu->signal_clicked().connect([this]{
					auto top = dynamic_cast<Gtk::Window*>(get_ancestor(GTK_TYPE_WINDOW));
					auto entity_uuid = UUID();
					auto package_uuid = UUID();
					{
						CreatePartDialog dia(top, &pool);
						if(dia.run() == Gtk::RESPONSE_OK) {
							entity_uuid = dia.get_entity();
							package_uuid = dia.get_package();
						}
					}
					if(!(entity_uuid && package_uuid))
						return;
					Gtk::FileChooserDialog fc(*top, "Save Part", Gtk::FILE_CHOOSER_ACTION_SAVE);
					fc.set_do_overwrite_confirmation(true);
					fc.set_current_name("package");
					fc.set_current_folder(Glib::build_filename(base_path, "parts"));
					fc.add_button("_Cancel", Gtk::RESPONSE_CANCEL);
					fc.add_button("_Save", Gtk::RESPONSE_ACCEPT);
					if(fc.run()==Gtk::RESPONSE_ACCEPT) {
						std::string fn = fc.get_filename();
						Part part(horizon::UUID::random());
						auto entity = pool.get_entity(entity_uuid);
						auto package = pool.get_package(package_uuid);
						part.attributes[Part::Attribute::MPN] = {false, entity->name};
						part.attributes[Part::Attribute::MANUFACTURER] = {false, entity->manufacturer};
						part.package = package;
						part.entity = entity;
						save_json_to_file(fn, part.serialize());
						spawn(PoolManagerProcess::Type::PART, {fn});
					}
				});
			}
			{
				auto bu = Gtk::manage(new Gtk::Button("Edit Part"));
				bbox->pack_start(*bu, false, false,0);
				bu->signal_clicked().connect([this, br]{
					auto uu = br->get_selected();
					if(!uu)
						return;
					auto path = pool.get_filename(ObjectType::PART, uu);
					spawn(PoolManagerProcess::Type::PART, {path});
				});
			}
			{
				auto bu = Gtk::manage(new Gtk::Button("Create Part from Part"));
				bbox->pack_start(*bu, false, false,0);
				bu->signal_clicked().connect([this, br]{
					auto uu = br->get_selected();
					if(!uu)
						return;
					auto base_part = pool.get_part(uu);
					auto top = dynamic_cast<Gtk::Window*>(get_ancestor(GTK_TYPE_WINDOW));
					Gtk::FileChooserDialog fc(*top, "Save Part", Gtk::FILE_CHOOSER_ACTION_SAVE);
					fc.set_do_overwrite_confirmation(true);
					fc.set_current_name(base_part->get_MPN()+".json");
					fc.set_current_folder(Glib::path_get_dirname(pool.get_filename(ObjectType::PART, uu)));
					fc.add_button("_Cancel", Gtk::RESPONSE_CANCEL);
					fc.add_button("_Save", Gtk::RESPONSE_ACCEPT);
					if(fc.run()==Gtk::RESPONSE_ACCEPT) {
						std::string fn = fc.get_filename();
						Part part(horizon::UUID::random());
						part.base = base_part;
						part.attributes[Part::Attribute::MPN] = {true, base_part->get_MPN()};
						part.attributes[Part::Attribute::MANUFACTURER] = {true, base_part->get_manufacturer()};
						part.attributes[Part::Attribute::VALUE] = {true, base_part->get_value()};
						save_json_to_file(fn, part.serialize());
						spawn(PoolManagerProcess::Type::PART, {fn});
					}
				});
			}

			bbox->show_all();

			box->pack_start(*bbox, false, false, 0);

			auto sep = Gtk::manage(new Gtk::Separator(Gtk::ORIENTATION_HORIZONTAL));
			sep->show();
			box->pack_start(*sep, false, false, 0);
			box->pack_start(*br, true, true, 0);
			box->show();

			append_page(*box, "Parts");
		}


	}

	bool PoolNotebook::can_close() {
		return processes.size() == 0 && part_wizard==nullptr;
	}

	void PoolNotebook::pool_update() {
		horizon::pool_update(base_path);
		for(auto &br: browsers) {
			br->search();
		}
		pool.clear();
		for(auto &it: processes) {
			it.second.reload();
		}
	}

	void PoolNotebook::populate() {

	}
}