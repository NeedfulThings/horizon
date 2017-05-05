#include "rules/rule.hpp"
#include "common.hpp"

namespace horizon {
	class RuleHoleSize: public Rule {
		public:
			RuleHoleSize(const UUID &uu);
			RuleHoleSize(const UUID &uu, const json &j);
			json serialize() const override;

			std::string get_brief() const;


			RuleMatch match;
			uint64_t diameter_min = 0.1_mm;
			uint64_t diameter_max = 10_mm;



	};
}
