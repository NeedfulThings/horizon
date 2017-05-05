#pragma once
#include <gtkmm.h>

namespace horizon {
	class RuleMatchEditor: public Gtk::Box {
		public:
			RuleMatchEditor(class RuleMatch *ma, class Core *c);
			typedef sigc::signal<void> type_signal_updated;
			type_signal_updated signal_updated() {return s_signal_updated;}

		private:
			Gtk::ComboBoxText *combo_mode = nullptr;
			Gtk::Stack *sel_stack = nullptr;
			class NetButton *net_button = nullptr;
			RuleMatch *match;
			class Core *core;
			type_signal_updated s_signal_updated;
	};
}
