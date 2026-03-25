// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#ifndef EMOTES_HPP
#define EMOTES_HPP

#include <ctime>
#include <memory>
#include <string>
#include <vector>

#include <common/cbasetypes.hpp>
#include <common/database.hpp>

class map_session_data;

enum e_cash_emote_pack_type : uint8 {
	CASH_EMOTE_PACK_ACCOUNT = 1,
	CASH_EMOTE_PACK_CHARACTER = 2,
};

enum e_cash_emote_use_status : uint8 {
	CASH_EMOTE_USE_FAIL_EXPIRED = 0,
	CASH_EMOTE_USE_FAIL_UNPURCHASED = 1,
	CASH_EMOTE_USE_FAIL_SKILL_LEVEL = 2,
	CASH_EMOTE_USE_FAIL_UNKNOWN = 3,
};

enum e_cash_emote_buy_status : uint8 {
	CASH_EMOTE_BUY_FAIL_NOT_ENOUGH_NYANGVINE = 0,
	CASH_EMOTE_BUY_FAIL_DATE = 1,
	CASH_EMOTE_BUY_FAIL_ALREADY_BOUGHT = 2,
	CASH_EMOTE_BUY_FAIL_ANOTHER_SALE_BOUGHT = 3, // Reserved by client.
	CASH_EMOTE_BUY_FAIL_SKILL_LEVEL = 4,
	CASH_EMOTE_BUY_FAIL_NOT_YET_ON_SALE = 5,
	CASH_EMOTE_BUY_FAIL_UNKNOWN = 6,
};

struct s_cash_emote_pack {
	uint16 id = 0;
	e_cash_emote_pack_type type = CASH_EMOTE_PACK_ACCOUNT;
	uint16 price = 0;
	std::vector<uint16> emotes;
	time_t sale_start = 0;
	time_t sale_end = 0;
	uint64 rental_period = 0;
};

class CashEmotesDatabase : public TypesafeYamlDatabase<uint16, s_cash_emote_pack> {
public:
	CashEmotesDatabase() : TypesafeYamlDatabase( "CASH_EMOTES_DB", 1 ){

	}

	const std::string getDefaultLocation() override;
	uint64 parseBodyNode( const ryml::NodeRef& node ) override;
};

extern CashEmotesDatabase cash_emotes_db;

void emotes_use( map_session_data* sd, uint16 pack_id, uint16 emote_id );
void emotes_buy( map_session_data* sd, uint16 pack_id, uint16 item_id, uint8 amount );
void emotes_get_player_packs( map_session_data* sd );

void do_init_emotes( void );
void do_final_emotes( void );

#endif /* EMOTES_HPP */
