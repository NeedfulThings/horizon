#include "imp_package.hpp"
#include "pool/part.hpp"
#include "footprint_generator/footprint_generator_window.hpp"
#include "parameter_window.hpp"
#include "header_button.hpp"
#include "widgets/chooser_buttons.hpp"
#include "widgets/pool_browser.hpp"
#include "canvas/canvas_gl.hpp"
#include "3d_view.hpp"
#include "board/board_layers.hpp"
#include "util/gtk_util.hpp"
#include "util/util.hpp"
#include "widgets/spin_button_dim.hpp"

namespace horizon {
	ImpPackage::ImpPackage(const std::string &package_filename, const std::string &pool_path):
			ImpLayer(pool_path),
			core_package(package_filename, *pool), fake_block(UUID::random()), fake_board(UUID::random(), fake_block) {
		core = &core_package;
		core_package.signal_tool_changed().connect(sigc::mem_fun(this, &ImpBase::handle_tool_change));


		key_seq_append_default(key_seq);
		key_seq.append_sequence({
				{{GDK_KEY_p, GDK_KEY_j}, 	ToolID::PLACE_JUNCTION},
				{{GDK_KEY_j},				ToolID::PLACE_JUNCTION},
				{{GDK_KEY_d, GDK_KEY_l}, 	ToolID::DRAW_LINE},
				{{GDK_KEY_l},				ToolID::DRAW_LINE},
				{{GDK_KEY_d, GDK_KEY_a}, 	ToolID::DRAW_ARC},
				{{GDK_KEY_a},				ToolID::DRAW_ARC},
				{{GDK_KEY_d, GDK_KEY_y}, 	ToolID::DRAW_POLYGON},
				{{GDK_KEY_y}, 				ToolID::DRAW_POLYGON},
				{{GDK_KEY_d, GDK_KEY_r}, 	ToolID::DRAW_POLYGON_RECTANGLE},
				{{GDK_KEY_p, GDK_KEY_t},	ToolID::PLACE_TEXT},
				{{GDK_KEY_t},				ToolID::PLACE_TEXT},
				{{GDK_KEY_p, GDK_KEY_p},	ToolID::PLACE_PAD},
				{{GDK_KEY_P},				ToolID::PLACE_PAD},
				{{GDK_KEY_i},				ToolID::EDIT_PAD_PARAMETER_SET},
		});
		key_seq.signal_update_hint().connect([this] (const std::string &s) {main_window->tool_hint_label->set_text(s);});

	}

	void ImpPackage::canvas_update() {
		canvas->update(*core_package.get_canvas_data());

	}


	class ModelEditor: public Gtk::Box {
		public:
			ModelEditor(ImpPackage *iimp, const UUID &iuu);
			const UUID uu;
			void update_all();

		private:
			ImpPackage *imp;
			Package *pkg = nullptr;
			Gtk::CheckButton *default_cb = nullptr;
			Gtk::Label *current_label = nullptr;

			SpinButtonDim *sp_x = nullptr;
			SpinButtonDim *sp_y = nullptr;
			SpinButtonDim *sp_z = nullptr;

			class SpinButtonAngle *sp_roll = nullptr;
			class SpinButtonAngle *sp_pitch = nullptr;
			class SpinButtonAngle *sp_yaw = nullptr;

	};

	static Gtk::Label *make_label(const std::string &text) {
		auto la = Gtk::manage(new Gtk::Label(text));
		la->get_style_context()->add_class("dim-label");
		la->set_halign(Gtk::ALIGN_END);
		return la;
	}

	class SpinButtonAngle: public Gtk::SpinButton {
		public:
			SpinButtonAngle();

		protected:
			 virtual int on_input(double* new_value);
			 virtual bool on_output();
	};


	SpinButtonAngle::SpinButtonAngle() : Gtk::SpinButton() {
		set_range(0, 65536);
		set_wrap(true);
		set_width_chars(6);
		set_increments(4096, 4096);
	}

	bool SpinButtonAngle::on_output() {
		auto adj = get_adjustment();
		double v = adj->get_value();

		std::stringstream stream;
		stream << std::fixed << std::setprecision(2) << (v/65536.0)*360 << "°";

		set_text(stream.str());
		return true;
	}

	int SpinButtonAngle::on_input(double *v) {
		auto txt = get_text();
		int64_t va = 0;
		try {
			va = (std::stod(txt)/360.0)*65536;
			*v = va;
		}
		catch (const std::exception& e) {
			return false;
		}
		return true;
	}

	class PlaceAtPadDialog: public Gtk::Dialog {
		public:
			PlaceAtPadDialog(Package *pkg);
			UUID selected_pad;

		private:
			Package *pkg;
	};

	class MyLabel: public Gtk::Label {
		public:
			MyLabel(const std::string &txt, const UUID &uu): Gtk::Label(txt), uuid(uu) {
				set_xalign(0);
				property_margin() = 5;
			}

			const UUID uuid;
	};

	PlaceAtPadDialog::PlaceAtPadDialog(Package *p): Gtk::Dialog("Place at pad", Gtk::DIALOG_MODAL|Gtk::DIALOG_USE_HEADER_BAR), pkg(p) {
		set_default_size(200, 400);
		auto sc = Gtk::manage(new Gtk::ScrolledWindow);
		sc->set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);

		auto lb = Gtk::manage(new Gtk::ListBox);
		lb->set_selection_mode(Gtk::SELECTION_NONE);
		lb->set_activate_on_single_click(true);
		lb->set_header_func(sigc::ptr_fun(header_func_separator));
		sc->add(*lb);

		std::vector<const Pad*> pads;
		for(const auto &it: pkg->pads) {
			pads.push_back(&it.second);
		}
		std::sort(pads.begin(), pads.end(), [](auto a, auto b){return strcmp_natural(a->name, b->name)<0;});

		for(const auto it: pads) {
			auto la = Gtk::manage(new MyLabel(it->name, it->uuid));
			lb->append(*la);
		}
		lb->signal_row_activated().connect([this] (Gtk::ListBoxRow *row) {
			auto la = dynamic_cast<MyLabel*>(row->get_child());
			selected_pad = la->uuid;
			response(Gtk::RESPONSE_OK);
		});

		sc->show_all();
		get_content_area()->set_border_width(0);
		get_content_area()->pack_start(*sc, true, true, 0);
	}

	ModelEditor::ModelEditor(ImpPackage *iimp, const UUID &iuu): Gtk::Box(Gtk::ORIENTATION_VERTICAL, 5), uu(iuu), imp(iimp) {
		pkg = imp->core_package.get_package();
		auto &model = pkg->models.at(uu);
		property_margin() = 10;
		auto entry = Gtk::manage(new Gtk::Entry);
		pack_start(*entry, false, false, 0);
		entry->show();
		entry->set_width_chars(45);
		entry->signal_focus_in_event().connect([this](GdkEventFocus *ev) {
			imp->current_model = uu;
			imp->view_3d_window->update();
			update_all();
			return false;
		});
		bind_widget(entry, model.filename);

		{
			auto box = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 5));
			auto delete_button = Gtk::manage(new Gtk::Button);
			delete_button->set_image_from_icon_name("list-remove-symbolic", Gtk::ICON_SIZE_BUTTON);
			delete_button->signal_clicked().connect([this] {
				pkg->models.erase(uu);
				if(pkg->default_model == uu) {
					if(pkg->models.size()) {
						pkg->default_model = pkg->models.begin()->first;
					}
					else {
						pkg->default_model = UUID();
					}
				}
				update_all();
				delete this->get_parent();
			});
			box->pack_end(*delete_button, false, false, 0);

			auto browse_button = Gtk::manage(new Gtk::Button("Browse..."));
			browse_button->signal_clicked().connect([this, entry] {
				auto model2 = pkg->get_model(uu);
				auto mfn = imp->ask_3d_model_filename(model2?model2->filename:"");
				if(mfn.size()) {
					entry->set_text(mfn);
					imp->view_3d_window->update(true);
				}

			});
			box->pack_end(*browse_button, false, false, 0);

			default_cb = Gtk::manage(new Gtk::CheckButton("Default"));
			if(pkg->default_model == uu) {
				default_cb->set_active(true);
			}
			default_cb->signal_toggled().connect([this] {
				if(default_cb->get_active()) {
					pkg->default_model = uu;
					imp->current_model = uu;
				}
				imp->view_3d_window->update();
				update_all();
			});
			box->pack_start(*default_cb, false, false, 0);

			current_label = Gtk::manage(new Gtk::Label("Current"));
			current_label->get_style_context()->add_class("dim-label");
			current_label->set_no_show_all(true);
			box->pack_start(*current_label, false, false, 0);

			box->show_all();
			pack_start(*box, false, false, 0);
		}

		{
			auto box = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 5));
			auto reset_button = Gtk::manage(new Gtk::Button("Reset placement"));
			reset_button->signal_clicked().connect([this] {
				sp_x->set_value(0);
				sp_y->set_value(0);
				sp_z->set_value(0);
				sp_roll->set_value(0);
				sp_pitch->set_value(0);
				sp_yaw->set_value(0);
			});
			box->pack_start(*reset_button, false, false, 0);

			auto place_at_pad_button = Gtk::manage(new Gtk::Button("Place at pad"));
			place_at_pad_button->signal_clicked().connect([this] {
				PlaceAtPadDialog dia(pkg);
				dia.set_transient_for(*imp->view_3d_window);
				if(dia.run() == Gtk::RESPONSE_OK) {
					auto &pad = pkg->pads.at(dia.selected_pad);
					sp_x->set_value(pad.placement.shift.x);
					sp_y->set_value(pad.placement.shift.y);
				}
			});
			box->pack_start(*place_at_pad_button, false, false, 0);

			box->show_all();
			pack_start(*box, false, false, 0);
		}

		auto placement_grid = Gtk::manage(new Gtk::Grid);
		placement_grid->set_hexpand_set(true);
		placement_grid->set_row_spacing(5);
		placement_grid->set_column_spacing(5);
		placement_grid->attach(*make_label("X"), 0, 0, 1, 1);
		placement_grid->attach(*make_label("Y"), 0, 1, 1, 1);
		placement_grid->attach(*make_label("Z"), 0, 2, 1, 1);
		std::set<Gtk::SpinButton*> placement_spin_buttons;
		{
			auto sp = Gtk::manage(new SpinButtonDim);
			sp->set_range(-100_mm, 100_mm);
			placement_grid->attach(*sp, 1, 0, 1, 1);
			bind_widget(sp, model.x);
			placement_spin_buttons.insert(sp);
			sp_x = sp;
		}
		{
			auto sp = Gtk::manage(new SpinButtonDim);
			sp->set_range(-100_mm, 100_mm);
			placement_grid->attach(*sp, 1, 1, 1, 1);
			bind_widget(sp, model.y);
			placement_spin_buttons.insert(sp);
			sp_y = sp;
		}
		{
			auto sp = Gtk::manage(new SpinButtonDim);
			sp->set_range(-100_mm, 100_mm);
			placement_grid->attach(*sp, 1, 2, 1, 1);
			bind_widget(sp, model.z);
			placement_spin_buttons.insert(sp);
			sp_z = sp;
		}

		{
			auto la = make_label("Roll");
			la->set_hexpand(true);
			placement_grid->attach(*la, 2, 0, 1, 1);
		}
		placement_grid->attach(*make_label("Pitch"), 2, 1, 1, 1);
		placement_grid->attach(*make_label("Yaw"), 2, 2, 1, 1);
		{
			auto sp = Gtk::manage(new SpinButtonAngle);
			placement_grid->attach(*sp, 3, 0, 1, 1);
			bind_widget(sp, model.roll);
			placement_spin_buttons.insert(sp);
			sp_roll = sp;
		}
		{
			auto sp = Gtk::manage(new SpinButtonAngle);
			placement_grid->attach(*sp, 3, 1, 1, 1);
			bind_widget(sp, model.pitch);
			placement_spin_buttons.insert(sp);
			sp_pitch = sp;
		}
		{
			auto sp = Gtk::manage(new SpinButtonAngle);
			placement_grid->attach(*sp, 3, 2, 1, 1);
			bind_widget(sp, model.yaw);
			placement_spin_buttons.insert(sp);
			sp_yaw = sp;
		}
		for(auto sp: placement_spin_buttons) {
			sp->signal_value_changed().connect([this] {
				imp->view_3d_window->update();
			});
		}


		placement_grid->show_all();
		pack_start(*placement_grid, false, false, 0);

		update_all();
	}

	void ModelEditor::update_all() {
		if(pkg->models.count(imp->current_model) == 0) {
			imp->current_model = pkg->default_model;
		}
		auto children = imp->models_listbox->get_children();
		for(auto ch: children) {
			auto ed = dynamic_cast<ModelEditor*>(dynamic_cast<Gtk::ListBoxRow*>(ch)->get_child());
			ed->default_cb->set_active(pkg->default_model == ed->uu);
			ed->current_label->set_visible(imp->current_model == ed->uu);
		}
	}

	std::string ImpPackage::ask_3d_model_filename(const std::string &current_filename) {
		GtkFileChooserNative *native = gtk_file_chooser_native_new ("Open",
			GTK_WINDOW(view_3d_window->gobj()),
			GTK_FILE_CHOOSER_ACTION_OPEN,
			"_Open",
			"_Cancel");
		auto chooser = Glib::wrap(GTK_FILE_CHOOSER(native));
		auto filter= Gtk::FileFilter::create();
		filter->set_name("STEP models");
		filter->add_pattern("*.step");
		filter->add_pattern("*.stp");
		chooser->add_filter(filter);
		if(current_filename.size()) {
			chooser->set_filename(Glib::build_filename(pool->get_base_path(), current_filename));
		}
		else {
			chooser->set_current_folder(pool->get_base_path());
		}

		while (1) {
			if(gtk_native_dialog_run (GTK_NATIVE_DIALOG (native))==GTK_RESPONSE_ACCEPT) {
				auto base_path = Gio::File::create_for_path(pool->get_base_path());
				std::string rel = base_path->get_relative_path(chooser->get_file());
				if(rel.size()) {
					#ifdef G_OS_WIN32
						replace_backslash(rel);
					#endif
					return rel;
				}
				else {
					Gtk::MessageDialog md(*view_3d_window,  "Model has to be in the pool directory", false /* use_markup */, Gtk::MESSAGE_ERROR, Gtk::BUTTONS_OK);
					md.run();
				}
			}
			else {
				return "";
			}
		}
		return "";
	}

	void ImpPackage::construct() {
		ImpLayer::construct_layer_box();

		main_window->set_title("Package - Interactive Manipulator");
		state_store = std::make_unique<WindowStateStore>(main_window, "imp-package");

		auto view_3d_button = Gtk::manage(new Gtk::Button("3D"));
		main_window->header->pack_start(*view_3d_button);
		view_3d_button->show();
		view_3d_button->signal_clicked().connect([this]{
			view_3d_window->update(); view_3d_window->present();
		});

		fake_board.set_n_inner_layers(0);
		fake_board.stackup.at(0).substrate_thickness = 1.6_mm;

		view_3d_window = View3DWindow::create(&fake_board, pool.get());
		view_3d_window->signal_request_update().connect([this] {
			fake_board.polygons.clear();
			{
				auto uu = UUID::random();
				auto &poly = fake_board.polygons.emplace(uu, uu).first->second;
				poly.layer = BoardLayers::L_OUTLINE;

				auto bb = core_package.get_package()->get_bbox();
				bb.first -= Coordi(5_mm, 5_mm);
				bb.second += Coordi(5_mm, 5_mm);

				poly.vertices.emplace_back(bb.first);
				poly.vertices.emplace_back(Coordi(bb.first.x, bb.second.y));
				poly.vertices.emplace_back(bb.second);
				poly.vertices.emplace_back(Coordi(bb.second.x, bb.first.y));
			}

			fake_board.packages.clear();
			{
				auto uu = UUID::random();
				auto &fake_package = fake_board.packages.emplace(uu, uu).first->second;
				fake_package.package = *core_package.get_package();
				fake_package.pool_package = core_package.get_package();
				fake_package.model = current_model;
			}
		});

		auto models_box = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL));
		{
			auto box = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 5));
			box->property_margin()=10;
			auto la = Gtk::manage(new Gtk::Label);
			la->set_markup("<b>3D Models</b>");
			la->set_xalign(0);
			box->pack_start(*la, false, false, 0);

			auto button_reload = Gtk::manage(new Gtk::Button("Reload models"));
			button_reload->signal_clicked().connect([this]{
				view_3d_window->update(true);
			});
			box->pack_end(*button_reload, false, false, 0);

			auto button_add = Gtk::manage(new Gtk::Button("Add model"));
			button_add->signal_clicked().connect([this] {
				auto mfn = ask_3d_model_filename();
				if(mfn.size()) {
					auto uu = UUID::random();
					auto pkg = core_package.get_package();
					if(pkg->models.size() == 0) { //first
						pkg->default_model = uu;
					}
					pkg->models.emplace(std::piecewise_construct, std::forward_as_tuple(uu), std::forward_as_tuple(uu, mfn));
					auto ed = Gtk::manage(new ModelEditor(this, uu));
					models_listbox->append(*ed);
					ed->show();
					current_model = uu;
					view_3d_window->update();
					ed->update_all();
				}
			});
			box->pack_end(*button_add, false, false, 0);

			models_box->pack_start(*box, false, false, 0);
		}
		{
			auto sep = Gtk::manage(new Gtk::Separator(Gtk::ORIENTATION_HORIZONTAL));
			models_box->pack_start(*sep, false, false, 0);
		}
		{
			auto sc = Gtk::manage(new Gtk::ScrolledWindow);
			sc->set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);

			models_listbox = Gtk::manage(new Gtk::ListBox);
			models_listbox->set_selection_mode(Gtk::SELECTION_NONE);
			models_listbox->set_header_func(sigc::ptr_fun(header_func_separator));
			models_listbox->set_activate_on_single_click(true);
			models_listbox->signal_row_activated().connect([this](Gtk::ListBoxRow *row) {
				auto ed = dynamic_cast<ModelEditor*>(row->get_child());
				current_model = ed->uu;
				view_3d_window->update();
				ed->update_all();
			});
			sc->add(*models_listbox);



			auto pkg = core_package.get_package();
			current_model = pkg->default_model;
			for(auto &it: pkg->models)  {
				auto ed = Gtk::manage(new ModelEditor(this, it.first));
				models_listbox->append(*ed);
				ed->show();
			}

			models_box->pack_start(*sc, true, true, 0);
		}

		models_box->show_all();
		view_3d_window->add_widget(models_box);

		footprint_generator_window = FootprintGeneratorWindow::create(main_window, &core_package);
		footprint_generator_window->signal_generated().connect(sigc::mem_fun(this, &ImpBase::canvas_update_from_pp));

		auto parameter_window = new ParameterWindow(main_window, &core_package.parameter_program_code, &core_package.parameter_set);
		{
			auto button = Gtk::manage(new Gtk::Button("Parameters..."));
			main_window->header->pack_start(*button);
			button->show();
			button->signal_clicked().connect([this, parameter_window]{parameter_window->present();});
		}
		{
			auto button = Gtk::manage(new Gtk::Button("Polygon expand"));
			parameter_window->add_button(button);
			button->signal_clicked().connect([this, parameter_window] {
				auto sel = canvas->get_selection();
				if(sel.size()==1) {
					auto &s = *sel.begin();
					if(s.type == ObjectType::POLYGON_EDGE || s.type == ObjectType::POLYGON_VERTEX) {
						auto poly = core.r->get_polygon(s.uuid);
						if(!poly->has_arcs()) {
							std::stringstream ss;
							ss.imbue(std::locale("C"));
							ss << "expand-polygon [ " << poly->parameter_class << " ";
							for(const auto &it: poly->vertices) {
								ss << it.position.x << " " << it.position.y << " ";
							}
							ss << "]\n";
							parameter_window->insert_text(ss.str());
						}

					}
				}
			});
		}
		{
			auto button = Gtk::manage(new Gtk::Button("Insert courtyard program"));
			parameter_window->add_button(button);
			button->signal_clicked().connect([this, parameter_window] {
				const Polygon *poly = nullptr;
				for(const auto &it: core_package.get_package()->polygons) {
					if(it.second.vertices.size() == 4 && !it.second.has_arcs() && it.second.parameter_class == "courtyard") {
						poly = &it.second;
						break;
					}
				}
				if(poly) {
					parameter_window->set_error_message("");
					Coordi a = poly->vertices.at(0).position;
					Coordi b = a;
					for(const auto &v: poly->vertices) {
						a = Coordi::min(a, v.position);
						b = Coordi::max(b, v.position);
					}
					auto c = (a+b)/2;
					auto d = b-a;
					std::stringstream ss;
					ss.imbue(std::locale("C"));
					ss << std::fixed << std::setprecision(3);
					ss << d.x/1e6 << "mm " << d.y/1e6 << "mm\n";
					ss << "get-parameter [ courtyard_expansion ]\n2 * +xy\nset-polygon [ courtyard rectangle ";
					ss << c.x/1e6 << "mm " << c.y/1e6 << "mm ]";
					parameter_window->insert_text(ss.str());
				}
				else {
					parameter_window->set_error_message("no courtyard polygon found: needs to have 4 vertices and the parameter class 'courtyard'");
				}
			});
		}
		parameter_window->signal_apply().connect([this, parameter_window] {
			if(core.r->tool_is_active())
				return;
			auto ps = core_package.get_package(false);
			auto r_compile = ps->parameter_program.set_code(core_package.parameter_program_code);
			if(r_compile.first) {
				parameter_window->set_error_message("<b>Compile error:</b>"+r_compile.second);
				return;
			}
			else {
				parameter_window->set_error_message("");
			}
			ps->parameter_set = core_package.parameter_set;
			auto r = ps->parameter_program.run(ps->parameter_set);
			if(r.first) {
				parameter_window->set_error_message("<b>Run error:</b>"+r.second);
				return;
			}
			else {
				parameter_window->set_error_message("");
			}
			core_package.rebuild();
			canvas_update();
		});

		auto header_button = Gtk::manage(new HeaderButton);
		header_button->set_label(core_package.get_package(false)->name);
		main_window->header->set_custom_title(*header_button);
		header_button->show();

		auto entry_name = header_button->add_entry("Name");
		auto entry_manufacturer = header_button->add_entry("Manufacturer");
		auto entry_tags = header_button->add_entry("Tags");




		auto browser_alt_button = Gtk::manage(new PoolBrowserButton(ObjectType::PACKAGE, pool.get()));
		browser_alt_button->get_browser()->set_show_none(true);
		header_button->add_widget("Alternate for", browser_alt_button);

		entry_name->signal_changed().connect([entry_name, header_button] {header_button->set_label(entry_name->get_text());});

		{
			auto pkg = core_package.get_package(false);
			entry_name->set_text(pkg->name);
			entry_manufacturer->set_text(pkg->manufacturer);
			std::stringstream s;
			std::copy(pkg->tags.begin(), pkg->tags.end(), std::ostream_iterator<std::string>(s, " "));
			entry_tags->set_text(s.str());
			if(pkg->alternate_for)
				browser_alt_button->property_selected_uuid() = pkg->alternate_for->uuid;
		}

		auto hamburger_menu = add_hamburger_menu();
		hamburger_menu->append("Import DXF", "win.import_dxf");

		add_tool_action(ToolID::IMPORT_DXF, "import_dxf");

		{
			auto button = Gtk::manage(new Gtk::Button("Footprint gen."));
			button->signal_clicked().connect([this]{
				footprint_generator_window->present();
				footprint_generator_window->show_all();
			});
			button->show();
			core.r->signal_tool_changed().connect([button](ToolID t){button->set_sensitive(t==ToolID::NONE);});
			main_window->header->pack_end(*button);
		}

		core_package.signal_save().connect([this, entry_name, entry_manufacturer, entry_tags, header_button, browser_alt_button]{
			auto pkg = core_package.get_package(false);
			std::stringstream ss(entry_tags->get_text());
			std::istream_iterator<std::string> begin(ss);
			std::istream_iterator<std::string> end;
			std::vector<std::string> tags(begin, end);
			pkg->tags.clear();
			pkg->tags.insert(tags.begin(), tags.end());
			pkg->name = entry_name->get_text();
			pkg->manufacturer = entry_manufacturer->get_text();
			UUID alt_uuid = browser_alt_button->property_selected_uuid();
			if(alt_uuid) {
				pkg->alternate_for = pool->get_package(alt_uuid);
			}
			else {
				pkg->alternate_for = nullptr;
			}
			header_button->set_label(pkg->name);
		});


	}

	ToolID ImpPackage::handle_key(guint k) {
		return key_seq.handle_key(k);
	}
}