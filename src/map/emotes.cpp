// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "emotes.hpp"

#include <algorithm>
#include <ctime>

#include <common/nullpo.hpp>
#include <common/random.hpp>
#include <common/socket.hpp> // last_tick

#include "battle.hpp"
#include "clif.hpp"
#include "log.hpp"
#include "pc.hpp"
#include "script.hpp"

CashEmotesDatabase cash_emotes_db;

namespace {

constexpr uint16 NYANGVINE_FRUIT = 6909;
constexpr uint64 DAY_AS_SECONDS = 60 * 60 * 24;

time_t emotes_date_to_timestamp( uint64 date_value ){
	std::tm date = {};

	date.tm_year = static_cast<int>( date_value / 10000 ) - 1900;
	date.tm_mon = static_cast<int>( ( date_value / 100 ) % 100 ) - 1;
	date.tm_mday = static_cast<int>( date_value % 100 );

	return std::mktime( &date );
}

std::string emotes_variable_name( const s_cash_emote_pack& pack, bool expiration ){
	std::string name;

	if( pack.type == CASH_EMOTE_PACK_ACCOUNT ){
		name = expiration ? "#cashemoteexpire_" : "#cashemote_";
	}else{
		name = expiration ? "cashemoteexpire_" : "cashemote_";
	}

	name += std::to_string( pack.id );

	return name;
}

} // namespace

const std::string CashEmotesDatabase::getDefaultLocation(){
	return std::string( db_path ) + "/cash_emotes_db.yml";
}

uint64 CashEmotesDatabase::parseBodyNode( const ryml::NodeRef& node ){
	uint16 pack_id;

	if( !this->asUInt16( node, "PackId", pack_id ) ){
		return 0;
	}

	std::shared_ptr<s_cash_emote_pack> pack = this->find( pack_id );
	bool exists = pack != nullptr;

	if( !exists ){
		if( !this->nodesExist( node, { "PackType", "PackPrice", "SaleStart", "SaleEnd", "RentalPeriod", "EmotesList" } ) ){
			return 0;
		}

		pack = std::make_shared<s_cash_emote_pack>();
	}

	pack->id = pack_id;

	if( this->nodeExists( node, "PackType" ) ){
		uint16 pack_type;

		if( !this->asUInt16( node, "PackType", pack_type ) ){
			return 0;
		}

		if( pack_type != CASH_EMOTE_PACK_ACCOUNT && pack_type != CASH_EMOTE_PACK_CHARACTER ){
			this->invalidWarning( node["PackType"], "PackType %hu is invalid. Allowed values are 1 and 2.\n", pack_type );
			return 0;
		}

		pack->type = static_cast<e_cash_emote_pack_type>( pack_type );
	}

	if( this->nodeExists( node, "PackPrice" ) ){
		uint16 price;

		if( !this->asUInt16( node, "PackPrice", price ) ){
			return 0;
		}

		if( price > 255 ){
			this->invalidWarning( node["PackPrice"], "PackPrice %hu exceeds client packet limit (255).\n", price );
			return 0;
		}

		pack->price = price;
	}

	if( this->nodeExists( node, "SaleStart" ) ){
		uint64 sale_start;

		if( !this->asUInt64( node, "SaleStart", sale_start ) ){
			return 0;
		}

		pack->sale_start = sale_start == 0 ? 0 : emotes_date_to_timestamp( sale_start );
	}

	if( this->nodeExists( node, "SaleEnd" ) ){
		uint64 sale_end;

		if( !this->asUInt64( node, "SaleEnd", sale_end ) ){
			return 0;
		}

		pack->sale_end = sale_end == 0 ? 0 : emotes_date_to_timestamp( sale_end );
	}

	if( this->nodeExists( node, "RentalPeriod" ) ){
		uint64 rental_period;

		if( !this->asUInt64( node, "RentalPeriod", rental_period ) ){
			return 0;
		}

		pack->rental_period = rental_period * DAY_AS_SECONDS;
	}

	if( this->nodeExists( node, "EmotesList" ) ){
		pack->emotes.clear();

		for( const ryml::NodeRef& emote_node : node["EmotesList"] ){
			if( !emote_node.is_val() ){
				this->invalidWarning( emote_node, "Unknown format, skipping.\n" );
				continue;
			}

			std::string emote_name( emote_node.val().str, emote_node.val().len );
			int64 emote_id;

			if( !script_get_constant( emote_name.c_str(), &emote_id ) ){
				this->invalidWarning( emote_node, "Unknown emotion \"%s\".\n", emote_name.c_str() );
				continue;
			}

			if( emote_id < ET_SURPRISE || emote_id >= ET_MAX ){
				this->invalidWarning( emote_node, "Emotion \"%s\" is out of range.\n", emote_name.c_str() );
				continue;
			}

			if( emote_id == ET_CHAT_PROHIBIT ){
				this->invalidWarning( emote_node, "Emotion \"%s\" is blocked from emote packs.\n", emote_name.c_str() );
				continue;
			}

			uint16 normalized_emote_id = static_cast<uint16>( emote_id );

			if( std::find( pack->emotes.begin(), pack->emotes.end(), normalized_emote_id ) == pack->emotes.end() ){
				pack->emotes.push_back( normalized_emote_id );
			}
		}
	}

	if( !exists ){
		this->put( pack_id, pack );
	}

	return 1;
}

void emotes_use( map_session_data* sd, uint16 pack_id, uint16 emote_id ){
#if PACKETVER_MAIN_NUM >= 20230705
	nullpo_retv( sd );

	if( battle_config.basic_skill_check != 0 && pc_checkskill( sd, NV_BASIC ) < 2 && pc_checkskill( sd, SU_BASIC_SKILL ) < 1 ){
		clif_emotion_expansion_use_fail( sd, pack_id, emote_id, CASH_EMOTE_USE_FAIL_SKILL_LEVEL );
		return;
	}

	if( sd->emotionlasttime + 1 >= time( nullptr ) ){
		sd->emotionlasttime = time( nullptr );
		clif_emotion_expansion_use_fail( sd, pack_id, emote_id, CASH_EMOTE_USE_FAIL_UNKNOWN );
		return;
	}

	sd->emotionlasttime = time( nullptr );

	if( battle_config.idletime_option & IDLE_EMOTION ){
		sd->idletime = last_tick;
	}

	if( battle_config.hom_idle_no_share && sd->hd && battle_config.idletime_hom_option & IDLE_EMOTION ){
		sd->idletime_hom = last_tick;
	}

	if( battle_config.mer_idle_no_share && sd->md && battle_config.idletime_mer_option & IDLE_EMOTION ){
		sd->idletime_mer = last_tick;
	}

	if( sd->state.block_action & PCBLOCK_EMOTION ){
		clif_emotion_expansion_use_fail( sd, pack_id, emote_id, CASH_EMOTE_USE_FAIL_UNKNOWN );
		return;
	}

	std::shared_ptr<s_cash_emote_pack> pack = cash_emotes_db.find( pack_id );

	if( pack == nullptr ){
		clif_emotion_expansion_use_fail( sd, pack_id, emote_id, CASH_EMOTE_USE_FAIL_UNKNOWN );
		return;
	}

	if( std::find( pack->emotes.begin(), pack->emotes.end(), emote_id ) == pack->emotes.end() ){
		clif_emotion_expansion_use_fail( sd, pack_id, emote_id, CASH_EMOTE_USE_FAIL_UNKNOWN );
		return;
	}

	if( pack->id != 0 ){
		const std::string has_pack_name = emotes_variable_name( *pack, false );
		int64 has_pack = pc_readglobalreg( sd, add_str( has_pack_name.c_str() ) );

		if( has_pack == 0 ){
			clif_emotion_expansion_use_fail( sd, pack_id, emote_id, CASH_EMOTE_USE_FAIL_UNPURCHASED );
			return;
		}

		if( pack->rental_period != 0 ){
			const std::string expire_name = emotes_variable_name( *pack, true );
			int64 expiration_time = pc_readglobalreg( sd, add_str( expire_name.c_str() ) );

			if( time( nullptr ) > expiration_time ){
				clif_emotion_expansion_use_fail( sd, pack_id, emote_id, CASH_EMOTE_USE_FAIL_EXPIRED );
				return;
			}
		}
	}

	if( battle_config.client_reshuffle_dice && emote_id >= ET_DICE1 && emote_id <= ET_DICE6 ){
		emote_id = static_cast<uint16>( rnd() % 6 + ET_DICE1 );
	}

	clif_emotion_expansion_use_success( *sd, pack_id, emote_id );
#endif
}

void emotes_buy( map_session_data* sd, uint16 pack_id, uint16 item_id, uint8 amount ){
#if PACKETVER_MAIN_NUM >= 20230802
	nullpo_retv( sd );

	if( battle_config.basic_skill_check != 0 && pc_checkskill( sd, NV_BASIC ) < 2 && pc_checkskill( sd, SU_BASIC_SKILL ) < 1 ){
		clif_emotion_expansion_buy_fail( sd, pack_id, CASH_EMOTE_BUY_FAIL_SKILL_LEVEL );
		return;
	}

	std::shared_ptr<s_cash_emote_pack> pack = cash_emotes_db.find( pack_id );

	if( pack == nullptr ){
		clif_emotion_expansion_buy_fail( sd, pack_id, CASH_EMOTE_BUY_FAIL_UNKNOWN );
		return;
	}

	uint16 required_amount = pack->price;

	if( required_amount > 0 && item_id != NYANGVINE_FRUIT ){
		clif_emotion_expansion_buy_fail( sd, pack_id, CASH_EMOTE_BUY_FAIL_UNKNOWN );
		return;
	}

	if( required_amount != 0 && amount != required_amount ){
		clif_emotion_expansion_buy_fail( sd, pack_id, CASH_EMOTE_BUY_FAIL_UNKNOWN );
		return;
	}

	time_t now = time( nullptr );

	if( pack->sale_start != 0 && pack->sale_start > now ){
		clif_emotion_expansion_buy_fail( sd, pack_id, CASH_EMOTE_BUY_FAIL_NOT_YET_ON_SALE );
		return;
	}

	if( pack->sale_end != 0 && pack->sale_end < now ){
		clif_emotion_expansion_buy_fail( sd, pack_id, CASH_EMOTE_BUY_FAIL_DATE );
		return;
	}

	const std::string has_pack_name = emotes_variable_name( *pack, false );
	int64 has_pack = pc_readglobalreg( sd, add_str( has_pack_name.c_str() ) );

	if( has_pack != 0 ){
		clif_emotion_expansion_buy_fail( sd, pack_id, CASH_EMOTE_BUY_FAIL_ALREADY_BOUGHT );
		return;
	}

	if( required_amount > 0 ){
		int32 inventory_index = pc_search_inventory( sd, item_id );

		if( inventory_index < 0 || sd->inventory.u.items_inventory[inventory_index].amount < required_amount ){
			clif_emotion_expansion_buy_fail( sd, pack_id, CASH_EMOTE_BUY_FAIL_NOT_ENOUGH_NYANGVINE );
			return;
		}

		if( pc_delitem( sd, inventory_index, required_amount, 0, 0, LOG_TYPE_CONSUME ) != 0 ){
			clif_emotion_expansion_buy_fail( sd, pack_id, CASH_EMOTE_BUY_FAIL_UNKNOWN );
			return;
		}
	}

	pc_setglobalreg( sd, add_str( has_pack_name.c_str() ), 1 );

	if( pack->rental_period != 0 ){
		const std::string expire_name = emotes_variable_name( *pack, true );
		int64 expiration_time = now + pack->rental_period;

		pc_setglobalreg( sd, add_str( expire_name.c_str() ), expiration_time );
		clif_emotion_expansion_buy_success( sd, pack_id, true, static_cast<uint32>( expiration_time ) );
		return;
	}

	clif_emotion_expansion_buy_success( sd, pack_id, false, 0 );
#endif
}

void emotes_get_player_packs( map_session_data* sd ){
#if PACKETVER_MAIN_NUM >= 20230705
	nullpo_retv( sd );

	std::vector<PACKET_ZC_EMOTION_EXPANSION_LIST_sub> packs;

	for( const auto& entry : cash_emotes_db ){
		std::shared_ptr<s_cash_emote_pack> pack = entry.second;

		const std::string has_pack_name = emotes_variable_name( *pack, false );
		int64 has_pack = pc_readglobalreg( sd, add_str( has_pack_name.c_str() ) );

		if( has_pack == 0 ){
			continue;
		}

		const std::string expire_name = emotes_variable_name( *pack, true );
		int64 expiration_time = pc_readglobalreg( sd, add_str( expire_name.c_str() ) );

		if( pack->rental_period != 0 && time( nullptr ) > expiration_time ){
			pc_setglobalreg( sd, add_str( has_pack_name.c_str() ), 0 );
			pc_setglobalreg( sd, add_str( expire_name.c_str() ), 0 );
			continue;
		}

		PACKET_ZC_EMOTION_EXPANSION_LIST_sub client_pack = {};

		client_pack.packId = pack->id;
		client_pack.isRented = pack->rental_period != 0;
		client_pack.timestamp = static_cast<uint32>( expiration_time );

		packs.push_back( client_pack );
	}

	clif_emotion_expansion_list( sd, packs );
#endif
}

void do_init_emotes( void ){
	cash_emotes_db.load();
}

void do_final_emotes( void ){
	cash_emotes_db.clear();
}
