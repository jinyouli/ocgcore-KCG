/*
 * Copyright (c) 2010-2015, Argon Sun (Fluorohydride)
 * Copyright (c) 2016-2025, Edoardo Lolletti (edo9300) <edoardo762@gmail.com>
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include <algorithm> //std::sort, std::copy_if, std::set_difference, std::copy
#include <iterator> //std::inserter
#include <memory> //std::make_unique
#include <set>
#include <utility> //std::move
#include <vector>
#include "card.h"
#include "duel.h"
#include "effect.h"
#include "field.h"
#include "group.h"
#include "interpreter.h"

int32_t field::negate_chain(uint8_t chaincount) {
	if(core.current_chain.size() == 0)
		return FALSE;
	if(chaincount > core.current_chain.size() || chaincount < 1)
		chaincount = static_cast<uint8_t>(core.current_chain.size());
	chain& pchain = core.current_chain[chaincount - 1];
	card* effect_handler = pchain.triggering_effect->get_handler();
	if(!(pchain.flag & CHAIN_DISABLE_ACTIVATE) && is_chain_negatable(pchain.chain_count)
		&& effect_handler->is_affect_by_effect(core.reason_effect)) {
		pchain.flag |= CHAIN_DISABLE_ACTIVATE;
		pchain.disable_reason = core.reason_effect;
		pchain.disable_player = core.reason_player;
		if((pchain.triggering_effect->type & EFFECT_TYPE_ACTIVATE) && (effect_handler->is_has_relation(pchain)) && (effect_handler->current.location == LOCATION_SZONE)) {
			effect_handler->set_status(STATUS_LEAVE_CONFIRMED, TRUE);
			effect_handler->set_status(STATUS_ACTIVATE_DISABLED, TRUE);
		}
		auto message = pduel->new_message(MSG_CHAIN_NEGATED);
		message->write<uint8_t>(chaincount);
		if(!is_flag(DUEL_RETURN_TO_DECK_TRIGGERS) &&
		   (pchain.triggering_location == LOCATION_DECK
			|| (pchain.triggering_location == LOCATION_EXTRA && (pchain.triggering_position & POS_FACEDOWN))))
			effect_handler->release_relation(pchain);
		return TRUE;
	}
	return FALSE;
}
int32_t field::disable_chain(uint8_t chaincount) {
	if(core.current_chain.size() == 0)
		return FALSE;
	if(chaincount > core.current_chain.size() || chaincount < 1)
		chaincount = static_cast<uint8_t>(core.current_chain.size());
	chain& pchain = core.current_chain[chaincount - 1];
	card* effect_handler = pchain.triggering_effect->get_handler();
	if(!(pchain.flag & CHAIN_DISABLE_EFFECT) && is_chain_disablable(pchain.chain_count)
		&& effect_handler->is_affect_by_effect(core.reason_effect)) {
		core.current_chain[chaincount - 1].flag |= CHAIN_DISABLE_EFFECT;
		core.current_chain[chaincount - 1].disable_reason = core.reason_effect;
		core.current_chain[chaincount - 1].disable_player = core.reason_player;
		auto message = pduel->new_message(MSG_CHAIN_DISABLED);
		message->write<uint8_t>(chaincount);
		if(!is_flag(DUEL_RETURN_TO_DECK_TRIGGERS) &&
		   (pchain.triggering_location == LOCATION_DECK
		   || (pchain.triggering_location == LOCATION_EXTRA && (pchain.triggering_position & POS_FACEDOWN))))
			effect_handler->release_relation(pchain);
		return TRUE;
	}
	return FALSE;
}
void field::change_chain_effect(uint8_t chaincount, int32_t rep_op) {
	if(core.current_chain.size() == 0)
		return;
	if(chaincount > core.current_chain.size() || chaincount < 1)
		chaincount = static_cast<uint8_t>(core.current_chain.size());
	chain& pchain = core.current_chain[chaincount - 1];
	pchain.replace_op = rep_op;
	if((pchain.triggering_effect->type & EFFECT_TYPE_ACTIVATE) && (pchain.triggering_effect->handler->current.location == LOCATION_SZONE)) {
		pchain.triggering_effect->handler->set_status(STATUS_LEAVE_CONFIRMED, TRUE);
	}
}
void field::change_target(uint8_t chaincount, group* targets) {
	if(core.current_chain.size() == 0)
		return;
	if(chaincount > core.current_chain.size() || chaincount < 1)
		chaincount = static_cast<uint8_t>(core.current_chain.size());
	auto ot = core.current_chain[chaincount - 1].target_cards;
	if(ot) {
		effect* te = core.current_chain[chaincount - 1].triggering_effect;
		for(auto& pcard : ot->container)
			pcard->release_relation(core.current_chain[chaincount - 1]);
		ot->container = targets->container;
		for(auto& pcard : ot->container)
			pcard->create_relation(core.current_chain[chaincount - 1]);
		if(te->is_flag(EFFECT_FLAG_CARD_TARGET)) {
			auto message = pduel->new_message(MSG_BECOME_TARGET);
			message->write<uint32_t>(ot->container.size());
			for(auto& pcard : ot->container) {
				message->write(pcard->get_info_location());
			}
		}
	}
}
void field::change_target_player(uint8_t chaincount, uint8_t playerid) {
	if(core.current_chain.size() == 0)
		return;
	if(chaincount > core.current_chain.size() || chaincount < 1)
		chaincount = static_cast<uint8_t>(core.current_chain.size());
	core.current_chain[chaincount - 1].target_player = playerid;
}
void field::change_target_param(uint8_t chaincount, int32_t param) {
	if(core.current_chain.size() == 0)
		return;
	if(chaincount > core.current_chain.size() || chaincount < 1)
		chaincount = static_cast<uint8_t>(core.current_chain.size());
	core.current_chain[chaincount - 1].target_param = param;
}
void field::remove_counter(uint32_t reason, card* pcard, uint32_t rplayer, uint8_t self, uint8_t oppo, uint32_t countertype, uint32_t count) {
	emplace_process<Processors::RemoveCounter>(reason, pcard, rplayer, self, oppo, countertype, count);
}
void field::remove_overlay_card(uint32_t reason, group* pgroup, uint32_t rplayer, uint8_t self, uint8_t oppo, uint16_t min, uint16_t max) {
	emplace_process<Processors::RemoveOverlay>(reason, pgroup, rplayer, self, oppo, min, max);
}
void field::xyz_overlay(card* target, card_set materials, bool send_materials_to_grave) {
	auto ng = pduel->new_group(std::move(materials));
	ng->is_readonly = true;
	emplace_process<Processors::XyzOverlay>(target, ng, send_materials_to_grave);
}
void field::xyz_overlay(card* target, card* material, bool send_materials_to_grave) {
	xyz_overlay(target, card_set{ material }, send_materials_to_grave);
}
void field::get_control(card_set targets, effect* reason_effect, uint8_t chose_player, uint8_t playerid, uint16_t reset_phase, uint8_t reset_count, uint32_t zone) {
	auto ng = pduel->new_group(std::move(targets));
	ng->is_readonly = true;
	emplace_process<Processors::GetControl>(reason_effect, chose_player, ng, playerid, reset_phase, reset_count, zone);
}
void field::get_control(card* target, effect* reason_effect, uint8_t chose_player, uint8_t playerid, uint16_t reset_phase, uint8_t reset_count, uint32_t zone) {
	get_control(card_set{ target }, reason_effect, chose_player, playerid, reset_phase, reset_count, zone);
}
void field::swap_control(effect* reason_effect, uint32_t reason_player, card_set targets1, card_set targets2, uint32_t reset_phase, uint32_t reset_count) {
	auto ng1 = pduel->new_group(std::move(targets1));
	ng1->is_readonly = true;
	auto ng2 = pduel->new_group(std::move(targets2));
	ng2->is_readonly = true;
	emplace_process<Processors::SwapControl>(reason_effect, reason_player, ng1, ng2, reset_phase, reset_count);
}
void field::swap_control(effect* reason_effect, uint32_t reason_player, card* pcard1, card* pcard2, uint32_t reset_phase, uint32_t reset_count) {
	swap_control(reason_effect, reason_player, card_set{ pcard1 }, card_set{ pcard2 }, reset_phase, reset_count);
}
void field::equip(uint8_t equip_player, card* equip_card, card* target, bool faceup, bool is_step) {
	emplace_process<Processors::Equip>(equip_player, equip_card, target, faceup, is_step);
}
void field::draw(effect* reason_effect, uint32_t reason, uint8_t reason_player, uint8_t playerid, uint16_t count) {
	emplace_process<Processors::Draw>(reason_effect, reason, reason_player, playerid, count);
}
void field::damage(effect* reason_effect, uint32_t reason, uint8_t reason_player, card* reason_card, uint8_t playerid, uint32_t amount, bool is_step) {
	if(reason & REASON_BATTLE)
		reason_effect = nullptr;
	else
		reason_card = nullptr;
	emplace_process<Processors::Damage>(reason_effect, reason, reason_player, reason_card, playerid, amount, is_step);
}
void field::recover(effect* reason_effect, uint32_t reason, uint32_t reason_player, uint32_t playerid, uint32_t amount, bool is_step) {
	emplace_process<Processors::Recover>(reason_effect, reason, reason_player, playerid, amount, is_step);
}
void field::summon(uint8_t sumplayer, card* target, effect* proc, bool ignore_count, uint8_t min_tribute, uint32_t zone) {
	emplace_process<Processors::SummonRule>(sumplayer, target, proc, ignore_count, min_tribute, zone);
}
void field::mset(uint8_t setplayer, card* target, effect* proc, bool ignore_count, uint8_t min_tribute, uint32_t zone) {
	emplace_process<Processors::MonsterSet>(setplayer, target, proc, ignore_count, min_tribute, zone);
}
void field::special_summon_rule(uint8_t sumplayer, card* target, uint32_t summon_type) {
	emplace_process<Processors::SpSummonRule>(sumplayer, target, summon_type);
}
void field::special_summon_rule_group(uint8_t sumplayer, uint32_t summon_type) {
	emplace_process<Processors::SpSummonRuleGroup>(sumplayer, summon_type);
}
void field::special_summon(card_set target, uint32_t sumtype, uint8_t sumplayer, uint8_t playerid, bool nocheck, bool nolimit, uint8_t positions, uint32_t zone) {
	if((positions & POS_FACEDOWN) && is_player_affected_by_effect(sumplayer, EFFECT_DEVINE_LIGHT))
		positions = (positions & POS_FACEUP) | ((positions & POS_FACEDOWN) >> 1);
	effect_set eset;
	filter_player_effect(sumplayer, EFFECT_FORCE_SPSUMMON_POSITION, &eset);
	for(auto& pcard : target) {
		pcard->temp.reason = pcard->current.reason;
		pcard->temp.reason_effect = pcard->current.reason_effect;
		pcard->temp.reason_player = pcard->current.reason_player;
		pcard->summon.type = (sumtype & 0xf00ffff) | SUMMON_TYPE_SPECIAL;
		pcard->summon.location = pcard->current.location;
		pcard->summon.sequence = pcard->current.sequence;
		pcard->summon.pzone = pcard->current.pzone;
		pcard->summon.player = sumplayer;
		pcard->current.reason = REASON_SPSUMMON;
		pcard->current.reason_effect = core.reason_effect;
		pcard->current.reason_player = core.reason_player;
		int pos = positions;
		for(auto& eff : eset) {
			if(eff->target) {
				pduel->lua->add_param<LuaParam::EFFECT>(eff);
				pduel->lua->add_param<LuaParam::CARD>(pcard);
				pduel->lua->add_param<LuaParam::INT>(core.reason_player);
				pduel->lua->add_param<LuaParam::INT>(pcard->summon.type);
				pduel->lua->add_param<LuaParam::INT>(pos);
				pduel->lua->add_param<LuaParam::INT>(playerid);
				pduel->lua->add_param<LuaParam::EFFECT>(core.reason_effect);
				if(!pduel->lua->check_condition(eff->target, 7))
					continue;
			}
			pos &= eff->get_value();
		}
		pcard->spsummon_param = (playerid << 24) + (nocheck << 16) + (nolimit << 8) + pos;
	}
	auto pgroup = pduel->new_group(std::move(target));
	pgroup->is_readonly = true;
	emplace_process<Processors::SpSummon>(core.reason_effect, core.reason_player, pgroup, zone);
}
void field::special_summon_step(card* target, uint32_t sumtype, uint8_t sumplayer, uint8_t playerid, bool nocheck, bool nolimit, uint8_t positions, uint32_t zone) {
	if((positions & POS_FACEDOWN) && is_player_affected_by_effect(sumplayer, EFFECT_DEVINE_LIGHT))
		positions = (positions & POS_FACEUP) | ((positions & POS_FACEDOWN) >> 1);
	if((positions & POS_FACEUP) && check_unique_onfield(target, playerid, LOCATION_MZONE))
		positions &= ~POS_FACEUP;
	effect_set eset;
	filter_player_effect(sumplayer, EFFECT_FORCE_SPSUMMON_POSITION, &eset);
	target->temp.reason = target->current.reason;
	target->temp.reason_effect = target->current.reason_effect;
	target->temp.reason_player = target->current.reason_player;
	target->summon.type = (sumtype & 0xf00ffff) | SUMMON_TYPE_SPECIAL;
	target->summon.location = target->current.location;
	target->summon.sequence = target->current.sequence;
	target->summon.pzone = target->current.pzone;
	target->summon.player = sumplayer;
	target->current.reason = REASON_SPSUMMON;
	target->current.reason_effect = core.reason_effect;
	target->current.reason_player = core.reason_player;
	for(auto& eff : eset) {
		if(eff->target) {
			pduel->lua->add_param<LuaParam::EFFECT>(eff);
			pduel->lua->add_param<LuaParam::CARD>(target);
			pduel->lua->add_param<LuaParam::INT>(core.reason_player);
			pduel->lua->add_param<LuaParam::INT>((sumtype & 0xf00ffff) | SUMMON_TYPE_SPECIAL);
			pduel->lua->add_param<LuaParam::INT>(positions);
			pduel->lua->add_param<LuaParam::INT>(playerid);
			pduel->lua->add_param<LuaParam::EFFECT>(core.reason_effect);
			if(!pduel->lua->check_condition(eff->target, 7))
				continue;
		}
		positions &= eff->get_value();
	}
	target->spsummon_param = (playerid << 24) + (nocheck << 16) + (nolimit << 8) + positions;
	emplace_process<Processors::SpSummonStep>(nullptr, target, zone);
}
void field::special_summon_complete(effect* reason_effect, uint8_t reason_player) {
	auto ng = pduel->new_group(std::move(core.special_summoning));
	core.special_summoning.clear();
	ng->is_readonly = true;
	emplace_process<Processors::SpSummon>(Step{ 1 }, reason_effect, reason_player, ng, 0);
}
void field::destroy(card_set targets, effect* reason_effect, uint32_t reason, uint8_t reason_player, uint8_t playerid, uint16_t destination, uint32_t sequence) {
	const auto destroy_canceled_end = core.destroy_canceled.cend();
	for(auto cit = targets.begin(); cit != targets.end();) {
		card* pcard = *cit;
		if(pcard->is_status(STATUS_DESTROY_CONFIRMED) && core.destroy_canceled.find(pcard) == destroy_canceled_end) {
			targets.erase(cit++);
			continue;
		}
		pcard->temp.reason = pcard->current.reason;
		pcard->current.reason = reason;
		if(reason_player != PLAYER_SELFDES) {
			pcard->temp.reason_effect = pcard->current.reason_effect;
			pcard->temp.reason_player = pcard->current.reason_player;
			if(reason_effect)
				pcard->current.reason_effect = reason_effect;
			pcard->current.reason_player = reason_player;
		}
		uint32_t p = playerid;
		if(!(destination & (LOCATION_HAND | LOCATION_DECK | LOCATION_REMOVED)))
			destination = LOCATION_GRAVE;
		if(destination && p == PLAYER_NONE)
			p = pcard->owner;
		if(destination & (LOCATION_GRAVE | LOCATION_REMOVED))
			p = pcard->owner;
		pcard->set_status(STATUS_DESTROY_CONFIRMED, TRUE);
		pcard->sendto_param.set(p, POS_FACEUP, destination, sequence);
		++cit;
	}
	auto ng = pduel->new_group(std::move(targets));
	ng->is_readonly = true;
	emplace_process<Processors::Destroy>(ng, reason_effect, reason, reason_player);
}
void field::destroy(card* target, effect* reason_effect, uint32_t reason, uint8_t reason_player, uint8_t playerid, uint16_t destination, uint32_t sequence) {
	destroy(card_set{ target }, reason_effect, reason, reason_player, playerid, destination, sequence);
}
void field::release(card_set targets, effect* reason_effect, uint32_t reason, uint8_t reason_player) {
	for(auto& pcard : targets) {
		pcard->temp.reason = pcard->current.reason;
		pcard->temp.reason_effect = pcard->current.reason_effect;
		pcard->temp.reason_player = pcard->current.reason_player;
		pcard->current.reason = reason;
		pcard->current.reason_effect = reason_effect;
		pcard->current.reason_player = reason_player;
		pcard->sendto_param.set(pcard->owner, POS_FACEUP, LOCATION_GRAVE);
	}
	auto ng = pduel->new_group(std::move(targets));
	ng->is_readonly = true;
	emplace_process<Processors::Release>(ng, reason_effect, reason, reason_player);
}
void field::release(card* target, effect* reason_effect, uint32_t reason, uint8_t reason_player) {
	release(card_set{ target }, reason_effect, reason, reason_player);
}
// set current.reason, sendto_param
// send-to in scripts: here->PROCESSOR_SENDTO, step 0
void field::send_to(card_set targets, effect* reason_effect, uint32_t reason, uint8_t reason_player, uint8_t playerid, uint16_t destination, uint32_t sequence, uint8_t position, bool ignore) {
	if(destination & LOCATION_ONFIELD)
		return;
	for(auto& pcard : targets) {
		pcard->temp.reason = pcard->current.reason;
		pcard->temp.reason_effect = pcard->current.reason_effect;
		pcard->temp.reason_player = pcard->current.reason_player;
		pcard->current.reason = reason;
		pcard->current.reason_effect = reason_effect;
		pcard->current.reason_player = reason_player;
		uint32_t p = playerid;
		// send to hand from deck and playerid not given => send to the hand of controler
		if(p == PLAYER_NONE && (destination & LOCATION_HAND) && (pcard->current.location & LOCATION_DECK) && pcard->current.controler == reason_player)
			p = reason_player;
		if(p == PLAYER_NONE)
			p = pcard->owner;
		if(destination == LOCATION_GRAVE && pcard->current.location == LOCATION_REMOVED)
			pcard->current.reason |= REASON_RETURN;
		auto pos = position;
		if(destination != LOCATION_REMOVED && !ignore)
			pos = POS_FACEUP;
		else if(position == 0)
			pos = pcard->current.position;
		pcard->sendto_param.set(p, pos, destination, sequence);
	}
	auto ng = pduel->new_group(std::move(targets));
	ng->is_readonly = true;
	emplace_process<Processors::SendTo>(ng, reason_effect, reason, reason_player);
}
void field::send_to(card* target, effect* reason_effect, uint32_t reason, uint8_t reason_player, uint8_t playerid, uint16_t destination, uint32_t sequence, uint8_t position, bool ignore) {
	send_to(card_set{ target }, reason_effect, reason, reason_player, playerid, destination, sequence, position, ignore);
}
//////kdiy///////
//void field::move_to_field(card* target, uint8_t move_player, uint8_t playerid, uint16_t destination, uint8_t positions, bool enable, uint8_t ret, uint8_t zone, bool rule, uint8_t reason, bool confirm) {
	//if(!(destination & (LOCATION_MZONE | LOCATION_MMZONE | LOCATION_EMZONE | LOCATION_SZONE | LOCATION_STZONE | LOCATION_PZONE | LOCATION_FZONE)) || !positions)
void field::move_to_field(card* target, uint8_t move_player, uint8_t playerid, uint16_t destination, uint8_t positions, bool enable, uint8_t ret, uint32_t zone, bool rule, uint8_t reason, bool confirm) {
	if(!(destination & (LOCATION_RMZONE | LOCATION_RSZONE | LOCATION_MZONE | LOCATION_MMZONE | LOCATION_EMZONE | LOCATION_SZONE | LOCATION_STZONE | LOCATION_PZONE | LOCATION_FZONE)) || !positions)
//////kdiy///////
		return;
	if(destination & LOCATION_PZONE && target->current.is_location(LOCATION_PZONE) && playerid == target->current.controler)
	    return;
	//////kdiy///////
	//if(destination == target->current.location && playerid == target->current.controler)
	uint8_t Rloc = 0;
	if(destination & LOCATION_RMZONE) {
		destination = LOCATION_MZONE;
	    Rloc = 0x40;
	}
	if(destination & LOCATION_RSZONE) {
		destination = LOCATION_SZONE;
	    Rloc = 0x80;
	}
	if(Rloc == 0 && (destination & LOCATION_SZONE) && ((zone >> 8) & 0xff))
	    Rloc = (zone >> 8) & 0x1f;
	if((destination == target->prev_temp.location && playerid == target->current.controler && (Rloc != 0x40 && Rloc != 0x80)) || (destination == target->current.location && playerid == target->current.controler && (Rloc == 0x40 || Rloc == 0x80)))
	//////kdiy///////
		return;
	uint8_t pzone = false;
	if(destination == LOCATION_PZONE) {
		destination = LOCATION_SZONE;
		pzone = true;
	}
	if(destination == LOCATION_FZONE) {
		destination = LOCATION_SZONE;
		zone = 0x1 << 5;
	}
	if(destination == LOCATION_MMZONE) {
		destination = LOCATION_MZONE;
		zone = (0x1 << 0) | (0x1 << 1) | (0x1 << 2) | (0x1 << 3) | (0x1 << 4);
	}
	if(destination == LOCATION_STZONE) {
		destination = LOCATION_SZONE;
		zone = (0x1 << 0) | (0x1 << 1) | (0x1 << 2) | (0x1 << 3) | (0x1 << 4);
	}
	if(destination == LOCATION_EMZONE) {
		destination = LOCATION_MZONE;
		zone = (0x1 << 5) | (0x1 << 6);
	}
	//////kdiy///////
	// target->to_field_param = (move_player << 24) + (playerid << 16) + ((destination & 0xff) << 8) + positions;
	target->to_field_param = (move_player << 28) + (playerid << 24) + (Rloc << 16) + ((destination & 0xff) << 8) + positions;
	//////kdiy///////
	emplace_process<Processors::MoveToField>(target, enable, ret, pzone, zone, rule, reason, confirm);
}
void field::change_position(card_set targets, effect* reason_effect, uint8_t reason_player, uint8_t au, uint8_t ad, uint8_t du, uint8_t dd, uint32_t flag, bool enable) {
	auto ng = pduel->new_group(std::move(targets));
	ng->is_readonly = true;
	for(auto& pcard : ng->container) {
		if(pcard->current.position == POS_FACEUP_ATTACK)
			pcard->position_param = au;
		else if(pcard->current.position == POS_FACEDOWN_DEFENSE)
			pcard->position_param = dd;
		else if(pcard->current.position == POS_FACEUP_DEFENSE)
			pcard->position_param = du;
		else
			pcard->position_param = ad;
		pcard->position_param |= flag;
	}
	emplace_process<Processors::ChangePos>(ng, reason_effect, reason_player, enable);
}
void field::change_position(card* target, effect* reason_effect, uint8_t reason_player, uint8_t npos, uint32_t flag, bool enable) {
	auto ng = pduel->new_group(target);
	ng->is_readonly = true;
	target->position_param = npos;
	target->position_param |= flag;
	emplace_process<Processors::ChangePos>(ng, reason_effect, reason_player, enable);
}
void field::operation_replace(uint32_t type, uint16_t step, group* targets) {
	int32_t is_destroy = (type == EFFECT_DESTROY_REPLACE) ? TRUE : FALSE;
	auto pr = effects.continuous_effect.equal_range(type);
	std::vector<effect*> opp_effects;
	for(auto eit = pr.first; eit != pr.second;) {
		effect* reffect = eit->second;
		++eit;
		if(reffect->get_handler_player() == infos.turn_player)
			emplace_process<Processors::OperationReplace>(Step{ step }, reffect, targets, nullptr, is_destroy);
		else
			opp_effects.push_back(reffect);
	}
	for(auto& peffect : opp_effects)
		emplace_process<Processors::OperationReplace>(Step{ step }, peffect, targets, nullptr, is_destroy);
}
void field::select_tribute_cards(card* target, uint8_t playerid, bool cancelable, uint16_t min, uint16_t max, uint8_t toplayer, uint32_t zone) {
	emplace_process<Processors::SelectTribute>(target, playerid, cancelable, min, max, toplayer, zone);
}
bool field::process(Processors::Draw& arg) {
	auto reason_effect = arg.reason_effect;
	auto reason = arg.reason;
	auto reason_player = arg.reason_player;
	auto playerid = arg.playerid;
	auto count = arg.count;
	switch(arg.step) {
	case 0: {
		card_vector cv;
		uint32_t drawn = 0;
		uint32_t public_count = 0;
		if(!(reason & REASON_RULE) && !is_player_can_draw(playerid)) {
			returns.set<int32_t>(0, 0);
			return TRUE;
		}
		if(count == 0) {
			returns.set<int32_t>(0, 0);
			return TRUE;
		}
		core.overdraw[playerid] = false;
		for(uint32_t i = 0; i < count; ++i) {
			if(player[playerid].list_main.size() == 0) {
				core.overdraw[playerid] = true;
				break;
			}
			++drawn;
			card* pcard = player[playerid].list_main.back();
			pcard->enable_field_effect(false);
			pcard->cancel_field_effect();
			player[playerid].list_main.pop_back();
			if(core.current_chain.size() > 0)
				core.just_sent_cards.insert(pcard);
			pcard->previous.controler = pcard->current.controler;
			pcard->previous.location = pcard->current.location;
			pcard->previous.sequence = pcard->current.sequence;
			pcard->previous.position = pcard->current.position;
			pcard->previous.pzone = pcard->current.pzone;
			pcard->current.controler = PLAYER_NONE;
			pcard->current.reason_effect = reason_effect;
			pcard->current.reason_player = reason_player;
			pcard->current.reason = reason | REASON_DRAW;
			pcard->current.location = 0;
			add_card(playerid, pcard, LOCATION_HAND, 0);
			pcard->enable_field_effect(true);
			effect* pub = pcard->is_affected_by_effect(EFFECT_PUBLIC);
			if(pub)
				++public_count;
			pcard->current.position = pub ? POS_FACEUP : POS_FACEDOWN;
			cv.push_back(pcard);
			pcard->reset(RESET_TOHAND, RESET_EVENT);
		}
		core.hint_timing[playerid] |= TIMING_DRAW + TIMING_TOHAND;
		adjust_instant();
		arg.count = drawn;
		auto& drawn_set = arg.drawn_set;
		drawn_set.clear();
		drawn_set.insert(cv.begin(), cv.end());
		if(drawn) {
			if(core.global_flag & GLOBALFLAG_DECK_REVERSE_CHECK) {
				if(player[playerid].list_main.size()) {
					card* ptop = player[playerid].list_main.back();
					if(core.deck_reversed || (ptop->current.position == POS_FACEUP_DEFENSE)) {
						auto message = pduel->new_message(MSG_DECK_TOP);
						message->write<uint8_t>(playerid);
						message->write<uint32_t>(drawn);
						message->write<uint32_t>(ptop->data.code);
						message->write<uint32_t>(ptop->current.position);
					}
				}
			}
			auto message = pduel->new_message(MSG_DRAW);
			message->write<uint8_t>(playerid);
			message->write<uint32_t>(drawn);
			for(const auto& pcard : cv) {
				message->write<uint32_t>(pcard->data.code);
				message->write<uint32_t>(pcard->current.position);
			}
			if(core.deck_reversed && (public_count < drawn)) {
				message = pduel->new_message(MSG_CONFIRM_CARDS);
				message->write<uint8_t>(1 - playerid);
				message->write<uint32_t>(drawn_set.size());
				for(auto& pcard : drawn_set) {
					message->write<uint32_t>(pcard->data.code);
					message->write<uint8_t>(pcard->current.controler);
					message->write<uint8_t>(pcard->current.location);
					message->write<uint32_t>(pcard->current.sequence);
				}
				shuffle(playerid, LOCATION_HAND);
			}
			for (auto& pcard : drawn_set) {
				if(pcard->owner != pcard->current.controler) {
					effect* deffect = pduel->new_effect();
					deffect->owner = pcard;
					deffect->code = 0;
					deffect->type = EFFECT_TYPE_SINGLE;
					deffect->flag[0] = EFFECT_FLAG_CANNOT_DISABLE | EFFECT_FLAG_CLIENT_HINT;
					deffect->description = 67;
					deffect->reset_flag = RESET_EVENT + 0x1fe0000;
					pcard->add_effect(deffect);
				}
				raise_single_event(pcard, nullptr, EVENT_DRAW, reason_effect, reason, reason_player, playerid, 0);
				raise_single_event(pcard, nullptr, EVENT_TO_HAND, reason_effect, reason, reason_player, playerid, 0);
				raise_single_event(pcard, nullptr, EVENT_MOVE, reason_effect, reason, reason_player, playerid, 0);
			}
			process_single_event();
			raise_event(drawn_set, EVENT_DRAW, reason_effect, reason, reason_player, playerid, drawn);
			raise_event(drawn_set, EVENT_TO_HAND, reason_effect, reason, reason_player, playerid, drawn);
			raise_event(drawn_set, EVENT_MOVE, reason_effect, reason, reason_player, playerid, drawn);
			process_instant_event();
		}
		return FALSE;
	}
	case 1: {
		core.operated_set.swap(arg.drawn_set);
		returns.set<uint32_t>(0, count);
		return TRUE;
	}
	}
	return TRUE;
}
bool field::process(Processors::Damage& arg) {
	auto reason_effect = arg.reason_effect;
	auto reason = arg.reason;
	auto reason_player = arg.reason_player;
	auto reason_card = arg.reason_card;
	auto playerid = arg.playerid;
	auto amount = arg.amount;
	auto is_step = arg.is_step;
	switch(arg.step) {
	case 0: {
		effect_set eset;
		returns.set<uint32_t>(0, amount);
		if(amount == 0)
			return TRUE;
		if(!(reason & REASON_RDAMAGE)) {
			filter_player_effect(playerid, EFFECT_REVERSE_DAMAGE, &eset);
			for(const auto& peff : eset) {
				pduel->lua->add_param<LuaParam::EFFECT>(reason_effect);
				pduel->lua->add_param<LuaParam::INT>(reason);
				pduel->lua->add_param<LuaParam::INT>(reason_player);
				pduel->lua->add_param<LuaParam::CARD>(reason_card);
				if(peff->check_value_condition(4)) {
					recover(reason_effect, (reason & REASON_RRECOVER) | REASON_RDAMAGE | REASON_EFFECT, reason_player, playerid, amount, is_step);
					arg.step = 2;
					return FALSE;
				}
			}
		}
		eset.clear();
		filter_player_effect(playerid, EFFECT_REFLECT_DAMAGE, &eset);
		for(const auto& peff : eset) {
			pduel->lua->add_param<LuaParam::EFFECT>(reason_effect);
			pduel->lua->add_param<LuaParam::INT>(amount);
			pduel->lua->add_param<LuaParam::INT>(reason);
			pduel->lua->add_param<LuaParam::INT>(reason_player);
			pduel->lua->add_param<LuaParam::CARD>(reason_card);
			if (peff->check_value_condition(5)) {
				playerid = 1 - playerid;
				arg.is_reflected = true;
				break;
			}
		}
		uint32_t val = amount;
		eset.clear();
		filter_player_effect(playerid, EFFECT_CHANGE_DAMAGE, &eset);
		for(const auto& peff : eset) {
			pduel->lua->add_param<LuaParam::EFFECT>(reason_effect);
			pduel->lua->add_param<LuaParam::INT>(val);
			pduel->lua->add_param<LuaParam::INT>(reason);
			pduel->lua->add_param<LuaParam::INT>(reason_player);
			pduel->lua->add_param<LuaParam::CARD>(reason_card);
			val = static_cast<uint32_t>(peff->get_value(5));
			returns.set<uint32_t>(0, val);
			if(val == 0)
				return TRUE;
		}
		arg.amount = val;
		if(is_step) {
			arg.step = 1;
			core.recover_damage_reserve.push_back(std::move(arg));
			return TRUE;
		}
		return FALSE;
	}
	case 1: {
		if(arg.is_reflected)
			playerid = 1 - playerid;
		if(arg.is_reflected || (reason & REASON_RRECOVER))
			arg.step = 2;
		core.hint_timing[playerid] |= TIMING_DAMAGE;
		//////kdiy/////////
		if (player[playerid].lp >= 8888888)
			player[playerid].lp = 8888888;
		else if (amount > 2000000) {
			amount = 8888888;	
			player[playerid].lp = 0;
		}
		else
		//////kdiy/////////
		player[playerid].lp -= amount;
		auto message = pduel->new_message(MSG_DAMAGE);
		message->write<uint8_t>(playerid);
		message->write<uint32_t>(amount);
		//////kdiy/////////
		message->write<uint32_t>(reason);
		//////kdiy/////////
		raise_event(reason_card, EVENT_DAMAGE, reason_effect, reason, reason_player, playerid, amount);
		//////kdiy/////////
		if(player[playerid].lp < 0) {
		    raise_event(reason_card, EVENT_ZERO_LP, reason_effect, reason, reason_player, playerid, amount);
			process_instant_event();
		}
		//////kdiy/////////
		if(reason == REASON_BATTLE && reason_card) {
			if((player[playerid].lp <= 0) && (core.attack_target == nullptr) && reason_card->is_affected_by_effect(EFFECT_MATCH_KILL) && !is_player_affected_by_effect(playerid, EFFECT_CANNOT_LOSE_LP)) {
				message = pduel->new_message(MSG_MATCH_KILL);
				message->write<uint32_t>(reason_card->data.code);
			}
			raise_single_event(reason_card, nullptr, EVENT_BATTLE_DAMAGE, nullptr, 0, reason_player, playerid, amount);
			raise_event(reason_card, EVENT_BATTLE_DAMAGE, nullptr, 0, reason_player, playerid, amount);
			process_single_event();
		}
		if(is_player_affected_by_effect(playerid, EFFECT_CANNOT_LOSE_LP) && player[playerid].lp < 0)
			player[playerid].lp = 0;
		process_instant_event();
		return FALSE;
	}
	case 2: {
		returns.set<uint32_t>(0, amount);
		return TRUE;
	}
	case 3: {
		returns.set<uint32_t>(0, 0);
		return TRUE;
	}
	case 10: {
		//dummy
		return TRUE;
	}
	}
	return TRUE;
}
bool field::process(Processors::Recover& arg) {
	auto reason_effect = arg.reason_effect;
	auto reason = arg.reason;
	auto reason_player = arg.reason_player;
	auto playerid = arg.playerid;
	auto amount = arg.amount;
	auto is_step = arg.is_step;
	switch(arg.step) {
	case 0: {
		effect_set eset;
		returns.set<uint32_t>(0, amount);
		if(amount == 0)
			return TRUE;
		if(!(reason & REASON_RRECOVER)) {
			filter_player_effect(playerid, EFFECT_REVERSE_RECOVER, &eset);
			for(const auto& peff : eset) {
				pduel->lua->add_param<LuaParam::EFFECT>(reason_effect);
				pduel->lua->add_param<LuaParam::INT>(reason);
				pduel->lua->add_param<LuaParam::INT>(reason_player);
				if(peff->check_value_condition(3)) {
					damage(reason_effect, (reason & REASON_RDAMAGE) | REASON_RRECOVER | REASON_EFFECT, reason_player, nullptr, playerid, amount, is_step);
					arg.step = 2;
					return FALSE;
				}
			}
		}
		//////kdiy/////////
		uint32_t val = amount;
		eset.clear();
		filter_player_effect(playerid, EFFECT_CHANGE_RECOVER, &eset);
		for (const auto& peff : eset) {
			pduel->lua->add_param<LuaParam::EFFECT>(reason_effect);
			pduel->lua->add_param<LuaParam::INT>(val);
			pduel->lua->add_param<LuaParam::INT>(reason);
			pduel->lua->add_param<LuaParam::INT>(reason_player);
			val = static_cast<uint32_t>(peff->get_value(4));
			returns.set<uint32_t>(0, val);
			if (val == 0)
				return TRUE;
		}
		arg.amount = val;
		//////kdiy/////////
		if(is_step) {
			arg.step = 1;
			core.recover_damage_reserve.push_back(std::move(arg));
			return TRUE;
		}
		return FALSE;
	}
	case 1: {
		if(reason & REASON_RDAMAGE)
			arg.step = 2;
		core.hint_timing[playerid] |= TIMING_RECOVER;
		//////kdiy/////////
		if (player[playerid].lp >= 8888888)
		    player[playerid].lp = 8888888;
		else if (amount > 2000000) {
			amount = 8888888;	
			player[playerid].lp = 8888888;
		}
		else
		//////kdiy/////////
		player[playerid].lp += amount;
		auto message = pduel->new_message(MSG_RECOVER);
		message->write<uint8_t>(playerid);
		message->write<uint32_t>(amount);
		raise_event(nullptr, EVENT_RECOVER, reason_effect, reason, reason_player, playerid, amount);
		process_instant_event();
		return FALSE;
	}
	case 2: {
		returns.set<uint32_t>(0, amount);
		return TRUE;
	}
	case 3: {
		returns.set<uint32_t>(0, 0);
		return TRUE;
	}
	case 10: {
		//dummy
		return TRUE;
	}
	}
	return TRUE;
}
bool field::process(Processors::PayLPCost& arg) {
	auto playerid = arg.playerid;
	auto cost = arg.cost;
	switch(arg.step) {
	case 0: {
		effect_set eset;
		int32_t val = cost;
		filter_player_effect(playerid, EFFECT_LPCOST_CHANGE, &eset);
		for(const auto& peff : eset) {
			pduel->lua->add_param<LuaParam::EFFECT>(core.reason_effect);
			pduel->lua->add_param<LuaParam::INT>(playerid);
			pduel->lua->add_param<LuaParam::INT>(val);
			val = peff->get_value(3);
		}
		if(val <= 0)
			return TRUE;
		arg.cost = val;
		tevent e;
		e.event_cards = nullptr;
		e.event_player = playerid;
		e.event_value = val;
		e.reason = 0;
		e.reason_effect = core.reason_effect;
		e.reason_player = playerid;
		core.select_options.clear();
		core.select_effects.clear();
		if(val <= player[playerid].lp) {
			core.select_options.push_back(11);
			core.select_effects.push_back(nullptr);
		}
		auto pr = effects.continuous_effect.equal_range(EFFECT_LPCOST_REPLACE);
		for(auto eit = pr.first; eit != pr.second;) {
			effect* peffect = eit->second;
			++eit;
			if(peffect->is_activateable(peffect->get_handler_player(), e)) {
				core.select_options.push_back(peffect->description);
				core.select_effects.push_back(peffect);
			}
		}
		if(core.select_options.size() == 0)
			return TRUE;
		if(core.select_options.size() == 1)
			returns.set<int32_t>(0, 0);
		else if(core.select_effects[0] == nullptr && core.select_effects.size() == 2)
			emplace_process<Processors::SelectEffectYesNo>(playerid, 218, core.select_effects[1]->handler);
		else
			emplace_process<Processors::SelectOption>(playerid);
		return FALSE;
	}
	case 1: {
		effect* peffect = core.select_effects[returns.at<int32_t>(0)];
		if(!peffect) {
			//////kdiy/////////
			if (player[playerid].lp >= 8888888)
				player[playerid].lp = 8888888;
			else if (cost > 2000000) {
				cost = 8888888;	
				player[playerid].lp = 0;
			}
			else
			//////kdiy/////////
			player[playerid].lp -= cost;
			auto message = pduel->new_message(MSG_PAY_LPCOST);
			message->write<uint8_t>(playerid);
			message->write<uint32_t>(cost);
			raise_event(nullptr, EVENT_PAY_LPCOST, core.reason_effect, 0, playerid, playerid, cost);
			process_instant_event();
			return TRUE;
		}
		tevent e;
		e.event_cards = nullptr;
		e.event_player = playerid;
		e.event_value = cost;
		e.reason = 0;
		e.reason_effect = core.reason_effect;
		e.reason_player = playerid;
		solve_continuous(playerid, peffect, e);
		return TRUE;
	}
	}
	return TRUE;
}
// rplayer rmoves counter from pcard or the field
// s,o: binary value indicating the available side
// from pcard: Card.RemoveCounter() -> here -> card::remove_counter() -> the script should raise EVENT_REMOVE_COUNTER if necessary
// from the field: Duel.RemoveCounter() -> here -> field::select_counter() -> the system raises EVENT_REMOVE_COUNTER automatically
bool field::process(Processors::RemoveCounter& arg) {
	auto reason = arg.reason;
	auto pcard = arg.pcard;
	auto rplayer = arg.rplayer;
	auto self = arg.self;
	auto oppo = arg.oppo;
	auto countertype = arg.countertype;
	auto count = arg.count;
	switch(arg.step) {
	case 0: {
		core.select_options.clear();
		core.select_effects.clear();
		if((pcard && pcard->get_counter(countertype) >= count) || (!pcard && get_field_counter(rplayer, self, oppo, countertype))) {
			core.select_options.push_back(10);
			core.select_effects.push_back(nullptr);
		}
		auto pr = effects.continuous_effect.equal_range(EFFECT_RCOUNTER_REPLACE + countertype);
		tevent e;
		e.event_cards = nullptr;
		e.event_player = rplayer;
		e.event_value = count;
		e.reason = reason;
		e.reason_effect = core.reason_effect;
		e.reason_player = rplayer;
		for(auto eit = pr.first; eit != pr.second;) {
			effect* peffect = eit->second;
			++eit;
			if(peffect->is_activateable(peffect->get_handler_player(), e)) {
				core.select_options.push_back(peffect->description);
				core.select_effects.push_back(peffect);
			}
		}
		returns.set<int32_t>(0, 0);
		if(core.select_options.size() == 0)
			return TRUE;
		if(core.select_options.size() == 1)
			returns.set<int32_t>(0, 0);
		else if(core.select_effects[0] == nullptr && core.select_effects.size() == 2)
			emplace_process<Processors::SelectEffectYesNo>(rplayer, 220, core.select_effects[1]->handler);
		else
			emplace_process<Processors::SelectOption>(rplayer);
		return FALSE;
	}
	case 1: {
		effect* peffect = core.select_effects[returns.at<int32_t>(0)];
		if(peffect) {
			tevent e;
			e.event_cards = nullptr;
			e.event_player = rplayer;
			e.event_value = count;
			e.reason = reason;
			e.reason_effect = core.reason_effect;
			e.reason_player = rplayer;
			solve_continuous(rplayer, peffect, e);
			arg.step = 3;
			return FALSE;
		}
		if(pcard) {
			returns.set<int32_t>(0,  pcard->remove_counter(countertype, count));
			arg.step = 3;
			return FALSE;
		}
		emplace_process<Processors::SelectCounter>(rplayer, countertype, count, self, oppo);
		return FALSE;
	}
	case 2: {
		for(uint32_t i = 0; i < core.select_cards.size(); ++i)
			if(returns.at<int16_t>(i) > 0)
				core.select_cards[i]->remove_counter(countertype, returns.at<int16_t>(i));
		return FALSE;
	}
	case 3: {
		raise_event(nullptr, EVENT_REMOVE_COUNTER + countertype, core.reason_effect, reason, rplayer, rplayer, count);
		process_instant_event();
		return FALSE;
	}
	case 4: {
		returns.set<int32_t>(0, 1);
		return TRUE;
	}
	}
	return TRUE;
}
bool field::process(Processors::RemoveOverlay& arg) {
	auto reason = arg.reason;
	auto pgroup = arg.pgroup;
	auto rplayer = arg.rplayer;
	auto self = arg.self;
	auto oppo = arg.oppo;
	auto min = arg.min;
	auto max = arg.max;
	switch(arg.step) {
	case 0: {
		core.select_options.clear();
		core.select_effects.clear();
		if(get_overlay_count(rplayer, self, oppo, pgroup) >= min) {
			core.select_options.push_back(12);
			core.select_effects.push_back(nullptr);
		}
		auto pr = effects.continuous_effect.equal_range(EFFECT_OVERLAY_REMOVE_REPLACE);
		tevent e;
		e.event_cards = nullptr;
		e.event_player = rplayer;
		e.event_value = min;
		e.reason = reason;
		e.reason_effect = core.reason_effect;
		e.reason_player = rplayer;
		for(auto eit = pr.first; eit != pr.second;) {
			effect* peffect = eit->second;
			++eit;
			if(peffect->is_activateable(peffect->get_handler_player(), e)) {
				core.select_options.push_back(peffect->description);
				core.select_effects.push_back(peffect);
			}
		}
		returns.set<int32_t>(0, 0);
		if(core.select_options.size() == 0)
			return TRUE;
		if(core.select_options.size() == 1)
			returns.set<int32_t>(0, 0);
		else if(core.select_effects[0] == nullptr && core.select_effects.size() == 2)
			emplace_process<Processors::SelectEffectYesNo>(rplayer, 219, core.select_effects[1]->handler);
		else
			emplace_process<Processors::SelectOption>(rplayer);
		return FALSE;
	}
	case 1: {
		effect* peffect = core.select_effects[returns.at<int32_t>(0)];
		if(peffect) {
			tevent e;
			e.event_cards = nullptr;
			e.event_player = rplayer;
			e.event_value = min + (max << 16);
			e.reason = reason;
			e.reason_effect = core.reason_effect;
			e.reason_player = rplayer;
			solve_continuous(rplayer, peffect, e);
			arg.has_used_overlay_remove_replace_effect = true;
		}
		return FALSE;
	}
	case 2: {
		uint16_t cancelable = FALSE;
		if(arg.has_used_overlay_remove_replace_effect) {
			int32_t replace_count = returns.at<int32_t>(0);
			if(replace_count >= max)
				return TRUE;
			min -= replace_count;
			max -= replace_count;
			if(min <= 0) {
				cancelable = TRUE;
				min = 0;
			}
			arg.replaced_amount = replace_count;
		}
		core.select_cards.clear();
		card_set cset;
		get_overlay_group(rplayer, self, oppo, &cset, pgroup);
		for(auto& xcard : cset)
			core.select_cards.push_back(xcard);
		auto message = pduel->new_message(MSG_HINT);
		message->write<uint8_t>(HINT_SELECTMSG);
		message->write<uint8_t>(rplayer);
		message->write<uint64_t>(519);
		emplace_process<Processors::SelectCard>(rplayer, cancelable, min, max);
		return FALSE;
	}
	case 3: {
		send_to(card_set{ return_cards.list.begin(), return_cards.list.end() }, core.reason_effect, reason, rplayer, PLAYER_NONE, LOCATION_GRAVE, 0, POS_FACEUP);
		return FALSE;
	}
	case 4: {
		returns.set<int32_t>(0, returns.at<int32_t>(0) + arg.replaced_amount);
		return TRUE;
	}
	}
	return TRUE;
}
bool field::process(Processors::XyzOverlay& arg) {
	auto target = arg.target;
	auto materials = arg.materials;
	auto send_materials_to_grave = arg.send_materials_to_grave;
	switch(arg.step) {
	case 0: {
		for(auto& pcard : materials->container) {
			if(!pcard->overlay_target)
				pcard->enable_field_effect(false);
		}
		if(!send_materials_to_grave)
			return FALSE;
		card_set to_grave;
		for(auto& pcard : materials->container) {
			to_grave.insert(pcard->xyz_materials.begin(), pcard->xyz_materials.end());
		}
		send_to(std::move(to_grave), nullptr, REASON_RULE + REASON_LOST_TARGET, PLAYER_NONE, PLAYER_NONE, LOCATION_GRAVE, 0, POS_FACEUP);
		return FALSE;
	}
	case 1: {
		card_set des, from_grave;
		card_vector cv;
		cv.reserve(materials->container.size());
		std::copy_if(materials->container.begin(), materials->container.end(), std::back_inserter(cv), [target](const card* pcard) { return pcard->overlay_target != target; });
		if(!send_materials_to_grave) {
			const auto prev_size = cv.size();
			for(auto& pcard : materials->container) {
				cv.insert(cv.begin(), pcard->xyz_materials.begin(), pcard->xyz_materials.end());
			}
			const auto cur_size = cv.size();
			if(cur_size - prev_size >= 2)
				std::sort(cv.begin(), cv.begin() + ((cur_size - prev_size) - 1), card::card_operation_sort);
			if(prev_size >= 2)
				std::sort(cv.begin() + (cur_size - prev_size), cv.end(), card::card_operation_sort);
		}
		duel::duel_message* decktop[2] = { nullptr, nullptr };
		const size_t s[2] = { player[0].list_main.size(), player[1].list_main.size() };
		if(core.global_flag & GLOBALFLAG_DECK_REVERSE_CHECK) {
			const auto m_end = materials->container.end();
			if(s[0] > 0 && materials->container.find(player[0].list_main.back()) != m_end) {
				decktop[0] = pduel->new_message(MSG_DECK_TOP);
				decktop[0]->write<uint8_t>(0);
			}
			if(s[1] > 0 && materials->container.find(player[1].list_main.back()) != m_end) {
				decktop[1] = pduel->new_message(MSG_DECK_TOP);
				decktop[1]->write<uint8_t>(1);
			}
		}
		for(auto& pcard : cv) {
			//kdiy////////
			if(pcard->is_affected_by_effect(EFFECT_IMMUNE_OVERLAY) && !((pcard->current.reason & REASON_RULE) && !(pcard->is_affected_by_effect(EFFECT_GOD_IMMUNE) && pcard->current.reason_effect && !pcard->is_affect_by_effect(pcard->current.reason_effect))))
			    continue;
		    //kdiy////////
			pcard->current.reason = REASON_XYZ + REASON_MATERIAL;
			pcard->reset(RESET_LEAVE + RESET_OVERLAY, RESET_EVENT);
			if(pcard->unique_code)
				remove_unique_card(pcard);
			if(pcard->equiping_target)
				pcard->unequip();
			for(auto cit = pcard->equiping_cards.begin(); cit != pcard->equiping_cards.end();) {
				card* equipc = *cit++;
				des.insert(equipc);
				equipc->unequip();
			}
			pcard->clear_card_target();
			auto message = pduel->new_message(MSG_MOVE);
			message->write<uint32_t>(pcard->data.code);
			message->write(pcard->get_info_location());
			if(pcard->overlay_target) {
				pcard->overlay_target->xyz_remove(pcard);
			} else {
				remove_card(pcard);
				add_to_disable_check_list(pcard);
				if(pcard->previous.location == LOCATION_GRAVE) {
					from_grave.insert(pcard);
					raise_single_event(pcard, nullptr, EVENT_LEAVE_GRAVE, core.reason_effect, 0, core.reason_player, 0, 0);
				}
			}
			target->xyz_add(pcard);
			message->write(pcard->get_info_location());
			message->write<uint32_t>(pcard->current.reason);
            ///kdiy///////////
			message->write<uint8_t>(pcard->current.reason_player);
            message->write<bool>(false);
            message->write<bool>(pcard == *cv.begin());
            message->write<bool>(false);
            ///kdiy///////////
		}
		auto writetopcard = [rev = core.deck_reversed, &decktop, &player=player, &s](int playerid) {
			if(!decktop[playerid])
				return;
			auto& msg = decktop[playerid];
			const auto& list = player[playerid].list_main;
			if(list.empty() || (!rev && list.back()->current.position != POS_FACEUP_DEFENSE))
				msg->data.clear();
			else {
				auto& prevcount = s[playerid];
				const auto* ptop = list.back();
				msg->write<uint32_t>(prevcount - list.size());
				msg->write<uint32_t>(ptop->data.code);
				msg->write<uint32_t>(ptop->current.position);
			}
		};
		writetopcard(0);
		writetopcard(1);
		if(from_grave.size()) {
			raise_event(std::move(from_grave), EVENT_LEAVE_GRAVE, core.reason_effect, 0, core.reason_player, 0, 0);
			process_single_event();
			process_instant_event();
		}
		if(des.size())
			destroy(std::move(des), nullptr, REASON_LOST_TARGET + REASON_RULE, PLAYER_NONE);
		else
			adjust_instant();
		if(target->current.location & LOCATION_ONFIELD)
			adjust_all();
		return FALSE;
	}
	}
	return TRUE;
}
bool field::process(Processors::GetControl& arg) {
	auto reason_effect = arg.reason_effect;
	auto chose_player = arg.chose_player;
	auto targets = arg.targets;
	auto playerid = arg.playerid;
	auto reset_phase = arg.reset_phase;
	auto reset_count = arg.reset_count;
	auto zone = arg.zone;
	switch(arg.step) {
	case 0: {
		arg.destroy_set.clear();
		for(auto cit = targets->container.begin(); cit != targets->container.end();) {
			card* pcard = *cit++;
			pcard->filter_disable_related_cards();
			bool change = true;
			if(pcard->overlay_target)
				change = false;
			if(pcard->current.controler == playerid)
				change = false;
			if(pcard->current.controler == PLAYER_NONE)
				change = false;
			///////////kdiy//////////
			//if(pcard->current.location != LOCATION_MZONE)
			if(!((pcard->current.location == LOCATION_MZONE && !pcard->is_affected_by_effect(EFFECT_SANCT_MZONE)) || (pcard->current.location == LOCATION_SZONE && pcard->is_affected_by_effect(EFFECT_ORICA_SZONE))))
			///////////kdiy//////////
				change = false;
			if(!pcard->is_capable_change_control())
				change = false;
			if(reason_effect && !pcard->is_affect_by_effect(reason_effect))
				change = false;
			if(!is_flag(DUEL_TRAP_MONSTERS_NOT_USE_ZONE) && ((pcard->get_type() & TYPE_TRAPMONSTER) && get_useable_count(pcard, playerid, LOCATION_SZONE, playerid, LOCATION_REASON_CONTROL) <= 0))
				change = false;
			if(!change)
				targets->container.erase(pcard);
		}
		int32_t fcount = get_useable_count(nullptr, playerid, LOCATION_MZONE, playerid, LOCATION_REASON_CONTROL, zone);
		if(fcount <= 0) {
			arg.destroy_set.swap(targets->container);
			arg.step = 5;
			return FALSE;
		}
		if((int32_t)targets->container.size() > fcount) {
			core.select_cards.clear();
			for(auto& pcard : targets->container)
				core.select_cards.push_back(pcard);
			auto message = pduel->new_message(MSG_HINT);
			message->write<uint8_t>(HINT_SELECTMSG);
			message->write<uint8_t>(playerid);
			message->write<uint64_t>(502);
			uint16_t ct = static_cast<uint16_t>(targets->container.size() - fcount);
			emplace_process<Processors::SelectCard>(playerid, false, ct, ct);
		} else
			arg.step = 1;
		return FALSE;
	}
	case 1: {
		arg.destroy_set.insert(return_cards.list.begin(), return_cards.list.end());
		for(auto& pcard : return_cards.list)
			targets->container.erase(pcard);
		return FALSE;
	}
	case 2: {
		for(auto& pcard : targets->container) {
			if(pcard->unique_code && (pcard->unique_location & LOCATION_MZONE))
				remove_unique_card(pcard);
		}
		targets->it = targets->container.begin();
		return FALSE;
	}
	case 3: {
		if(targets->it == targets->container.end()) {
			adjust_instant();
			arg.step = 4;
			return FALSE;
		}
		card* pcard = *targets->it;
	    ///////kdiy///////
		pcard->prev_temp.location = LOCATION_MZONE;	
		effect* oeffect = is_player_affected_by_effect(playerid,EFFECT_ORICA);
	    if(oeffect && !pcard->is_affected_by_effect(EFFECT_ORICA_SZONE)) {
			effect* deffect = pduel->new_effect();
			deffect->owner = oeffect->owner;
			deffect->code = EFFECT_ORICA_SZONE;
			deffect->type = EFFECT_TYPE_SINGLE;
			deffect->flag[0] = EFFECT_FLAG_CANNOT_DISABLE | EFFECT_FLAG_IGNORE_IMMUNE | EFFECT_FLAG_UNCOPYABLE;
			deffect->reset_flag = RESET_EVENT+0x1fe0000+RESET_CONTROL-RESET_TURN_SET;
			pcard->add_effect(deffect);
            pcard->reset(EFFECT_SANCT_MZONE, RESET_CODE);
		}
		///////kdiy///////
		move_to_field(pcard, (chose_player == PLAYER_NONE) ? playerid : chose_player, playerid, LOCATION_MZONE, pcard->current.position, FALSE, 0, zone);					
		return FALSE;
	}
	case 4: {
		card* pcard = *targets->it;
		pcard->set_status(STATUS_ATTACK_CANCELED, TRUE);
		set_control(pcard, playerid, reset_phase, reset_count);
		pcard->reset(RESET_CONTROL, RESET_EVENT);
	    ///////kdiy///////
		effect* oeffect = is_player_affected_by_effect(playerid,EFFECT_ORICA);
	    if(oeffect && !pcard->is_affected_by_effect(EFFECT_ORICA_SZONE)) {
			effect* deffect = pduel->new_effect();
			deffect->owner = oeffect->owner;
			deffect->code = EFFECT_ORICA_SZONE;
			deffect->type = EFFECT_TYPE_SINGLE;
			deffect->flag[0] = EFFECT_FLAG_CANNOT_DISABLE | EFFECT_FLAG_IGNORE_IMMUNE | EFFECT_FLAG_UNCOPYABLE;
			deffect->reset_flag = RESET_EVENT+0x1fe0000+RESET_CONTROL-RESET_TURN_SET;
			pcard->add_effect(deffect);
            pcard->reset(EFFECT_SANCT_MZONE, RESET_CODE);
		}
		///////kdiy///////
		pcard->filter_disable_related_cards();
		++targets->it;
		arg.step = 2;
		return FALSE;
	}
	case 5: {
		uint8_t reason_player = chose_player == PLAYER_NONE ? PLAYER_NONE : core.reason_player;
		for(auto cit = targets->container.begin(); cit != targets->container.end(); ) {
			card* pcard = *cit++;
			if(!(pcard->current.location & LOCATION_ONFIELD)) {
				targets->container.erase(pcard);
				continue;
			}
			if(pcard->unique_code && (pcard->unique_location & LOCATION_MZONE))
				add_unique_card(pcard);
			raise_single_event(pcard, nullptr, EVENT_CONTROL_CHANGED, reason_effect, REASON_EFFECT, reason_player, playerid, 0);
			raise_single_event(pcard, nullptr, EVENT_MOVE, reason_effect, REASON_EFFECT, reason_player, playerid, 0);
		}
		if(targets->container.size()) {
			raise_event(targets->container, EVENT_CONTROL_CHANGED, reason_effect, REASON_EFFECT, reason_player, playerid, 0);
			raise_event(targets->container, EVENT_MOVE, reason_effect, REASON_EFFECT, reason_player, playerid, 0);
		}
		process_single_event();
		process_instant_event();
		return FALSE;
	}
	case 6: {
		if(arg.destroy_set.size())
			destroy(std::move(arg.destroy_set), nullptr, REASON_RULE, PLAYER_NONE);
		return FALSE;
	}
	case 7: {
		core.operated_set = targets->container;
		returns.set<int32_t>(0, static_cast<int32_t>(targets->container.size()));
		return TRUE;
	}
	}
	return TRUE;
}
bool field::process(Processors::SwapControl& arg) {
	auto reason_effect = arg.reason_effect;
	auto reason_player = arg.reason_player;
	auto targets1 = arg.targets1;
	auto targets2 = arg.targets2;
	auto reset_phase = arg.reset_phase;
	auto reset_count = arg.reset_count;
	switch(arg.step) {
	case 0: {
		arg.step = 9;
		if(targets1->container.size() == 0)
			return FALSE;
		if(targets2->container.size() == 0)
			return FALSE;
		if(targets1->container.size() != targets2->container.size())
			return FALSE;
		for(auto& pcard : targets1->container)
			pcard->filter_disable_related_cards();
		for(auto& pcard : targets2->container)
			pcard->filter_disable_related_cards();
		auto cit1 = targets1->container.begin();
		auto cit2 = targets2->container.begin();
		uint8_t p1 = (*cit1)->current.controler, p2 = (*cit2)->current.controler;
		if(p1 == p2 || p1 == PLAYER_NONE || p2 == PLAYER_NONE)
			return FALSE;
		for(auto& pcard : targets1->container) {
			if(pcard->overlay_target)
				return FALSE;
			if(pcard->current.controler != p1)
				return FALSE;
			///////////kdiy//////////
			//if(pcard->current.location != LOCATION_MZONE)
			if(!((pcard->current.location == LOCATION_MZONE && !pcard->is_affected_by_effect(EFFECT_SANCT_MZONE)) || (pcard->current.location == LOCATION_SZONE && pcard->is_affected_by_effect(EFFECT_ORICA_SZONE))))
			///////////kdiy//////////
				return FALSE;
			if(!pcard->is_capable_change_control())
				return FALSE;
			if((reason_effect && !pcard->is_affect_by_effect(reason_effect)))
				return FALSE;
		}
		for(auto& pcard : targets2->container) {
			if(pcard->overlay_target)
				return FALSE;
			if(pcard->current.controler != p2)
				return FALSE;
			///////////kdiy//////////
			//if(pcard->current.location != LOCATION_MZONE)
			if(!((pcard->current.location == LOCATION_MZONE && !pcard->is_affected_by_effect(EFFECT_SANCT_MZONE)) || (pcard->current.location == LOCATION_SZONE && pcard->is_affected_by_effect(EFFECT_ORICA_SZONE))))
			///////////kdiy//////////
				return FALSE;
			if(!pcard->is_capable_change_control())
				return FALSE;
			if((reason_effect && !pcard->is_affect_by_effect(reason_effect)))
				return FALSE;
		}
		int32_t ct = get_useable_count(nullptr, p1, LOCATION_MZONE, reason_player, LOCATION_REASON_CONTROL);
		for(auto& pcard : targets1->container) {
			if(pcard->current.sequence >= 5)
				--ct;
		}
		if(ct < 0)
			return FALSE;
		ct = get_useable_count(nullptr, p2, LOCATION_MZONE, reason_player, LOCATION_REASON_CONTROL);
		for(auto& pcard : targets2->container) {
			if(pcard->current.sequence >= 5)
				--ct;
		}
		if(ct < 0)
			return FALSE;
		for(auto& pcard : targets1->container) {
			if(pcard->unique_code && (pcard->unique_location & LOCATION_MZONE))
				remove_unique_card(pcard);
		}
		for(auto& pcard : targets2->container) {
			if(pcard->unique_code && (pcard->unique_location & LOCATION_MZONE))
				remove_unique_card(pcard);
		}
		targets1->it = targets1->container.begin();
		targets2->it = targets2->container.begin();
		arg.step = 0;
		return FALSE;
	}
	case 1: {
		if(targets1->it == targets1->container.end()) {
			arg.step = 3;
			return FALSE;
		}
		card* pcard1 = *targets1->it;
		uint8_t p1 = pcard1->current.controler;
		uint8_t s1 = pcard1->current.sequence;
		uint32_t flag;
	    ///////kdiy///////
		card* pcard2 = *targets2->it;
		uint8_t p2 = pcard2->current.controler;
		effect* oeffect = is_player_affected_by_effect(p1,EFFECT_ORICA);
	    if(oeffect && !pcard2->is_affected_by_effect(EFFECT_ORICA_SZONE)) {
			effect* deffect = pduel->new_effect();
		    deffect->owner = oeffect->owner;
		    deffect->code = EFFECT_ORICA_SZONE;
		    deffect->type = EFFECT_TYPE_SINGLE;
		    deffect->flag[0] = EFFECT_FLAG_CANNOT_DISABLE | EFFECT_FLAG_IGNORE_IMMUNE | EFFECT_FLAG_UNCOPYABLE;
			deffect->reset_flag = RESET_EVENT+0x1fe0000+RESET_CONTROL-RESET_TURN_SET;
		    pcard2->add_effect(deffect);
            pcard2->reset(EFFECT_SANCT_MZONE, RESET_CODE);
	    }
		effect* oeffect2 = is_player_affected_by_effect(p2,EFFECT_ORICA);
	    if(oeffect2 && !pcard1->is_affected_by_effect(EFFECT_ORICA_SZONE)) {
			effect* deffect = pduel->new_effect();
		    deffect->owner = oeffect2->owner;
			deffect->code = EFFECT_ORICA_SZONE;
			deffect->type = EFFECT_TYPE_SINGLE;
			deffect->flag[0] = EFFECT_FLAG_CANNOT_DISABLE | EFFECT_FLAG_IGNORE_IMMUNE | EFFECT_FLAG_UNCOPYABLE;
			deffect->reset_flag = RESET_EVENT+0x1fe0000+RESET_CONTROL-RESET_TURN_SET;
			pcard1->add_effect(deffect);
            pcard1->reset(EFFECT_SANCT_MZONE, RESET_CODE);
        }
	    ///////kdiy///////
		get_useable_count(nullptr, p1, LOCATION_MZONE, reason_player, LOCATION_REASON_CONTROL, 0xff, &flag);
		///////////kdiy//////////
		if(is_player_affected_by_effect(p1, EFFECT_ORICA))  {
			if(pcard1->current.location == LOCATION_MZONE)
				flag = (flag & ~(1 << s1) & 0x1fff) | ~0x1f1f;
			else
				flag = (flag & ~(256 << s1) & 0x1fff) | ~0x1f1f;
		} else
		///////////kdiy//////////
		flag = (flag & ~(1 << s1) & 0xff) | ~0x1f;			
		auto message = pduel->new_message(MSG_HINT);
		message->write<uint8_t>(HINT_SELECTMSG);
		message->write<uint8_t>(p1);
		message->write<uint64_t>(pcard2->data.code);
		emplace_process<Processors::SelectPlace>(p1, flag, 1);
		return FALSE;
	}
	case 2: {
		arg.self_selected_sequence = returns.at<int8_t>(2);
		card* pcard2 = *targets2->it;
		uint8_t p2 = pcard2->current.controler;
		uint8_t s2 = pcard2->current.sequence;
		//kdiy///////
		pcard2->temp.location = returns.at<int8_t>(1);
		//kdiy///////
		uint32_t flag;
		get_useable_count(nullptr, p2, LOCATION_MZONE, reason_player, LOCATION_REASON_CONTROL, 0xff, &flag);
		//kdiy///////
		card* pcard1 = *targets1->it;
		if(is_player_affected_by_effect(p2, EFFECT_ORICA))  {
			if(pcard2->current.location == LOCATION_MZONE)
				flag = (flag & ~(1 << s2) & 0x1fff) | ~0x1f1f;
			else
				flag = (flag & ~(256 << s2) & 0x1fff) | ~0x1f1f;
		} else
		///////////kdiy//////////
		flag = (flag & ~(1 << s2) & 0xff) | ~0x1f;
		auto message = pduel->new_message(MSG_HINT);
		message->write<uint8_t>(HINT_SELECTMSG);
		message->write<uint8_t>(p2);
		message->write<uint64_t>(pcard1->data.code);
		emplace_process<Processors::SelectPlace>(p2, flag, 1);
		return FALSE;
	}
	case 3: {
		card* pcard1 = *targets1->it;
		card* pcard2 = *targets2->it;
		uint8_t p1 = pcard1->current.controler, p2 = pcard2->current.controler;
		uint8_t new_s1 = arg.self_selected_sequence;
		uint8_t new_s2 = returns.at<int8_t>(2);
		//kdiy///////
		pcard1->temp.location = returns.at<int8_t>(1);
		//kdiy///////
		swap_card(pcard1, pcard2, new_s1, new_s2);
		pcard1->reset(RESET_CONTROL, RESET_EVENT);
		pcard2->reset(RESET_CONTROL, RESET_EVENT);
		set_control(pcard1, p2, reset_phase, reset_count);
		set_control(pcard2, p1, reset_phase, reset_count);
	    ///////kdiy///////
		effect* oeffect = is_player_affected_by_effect(p1,EFFECT_ORICA);
	    if(oeffect && !pcard2->is_affected_by_effect(EFFECT_ORICA_SZONE)) {
			effect* deffect = pduel->new_effect();
		    deffect->owner = oeffect->owner;
		    deffect->code = EFFECT_ORICA_SZONE;
		    deffect->type = EFFECT_TYPE_SINGLE;
		    deffect->flag[0] = EFFECT_FLAG_CANNOT_DISABLE | EFFECT_FLAG_IGNORE_IMMUNE | EFFECT_FLAG_UNCOPYABLE;
			deffect->reset_flag = RESET_EVENT+0x1fe0000+RESET_CONTROL-RESET_TURN_SET;
		    pcard2->add_effect(deffect);
            pcard2->reset(EFFECT_SANCT_MZONE, RESET_CODE);
	    }
		effect* oeffect2 = is_player_affected_by_effect(p2,EFFECT_ORICA);
	    if(oeffect2 && !pcard1->is_affected_by_effect(EFFECT_ORICA_SZONE)) {
			effect* deffect = pduel->new_effect();
		    deffect->owner = oeffect2->owner;
			deffect->code = EFFECT_ORICA_SZONE;
			deffect->type = EFFECT_TYPE_SINGLE;
			deffect->flag[0] = EFFECT_FLAG_CANNOT_DISABLE | EFFECT_FLAG_IGNORE_IMMUNE | EFFECT_FLAG_UNCOPYABLE;
			deffect->reset_flag = RESET_EVENT+0x1fe0000+RESET_CONTROL-RESET_TURN_SET;
			pcard1->add_effect(deffect);
            pcard1->reset(EFFECT_SANCT_MZONE, RESET_CODE);
        }
		pcard1->temp.location = 0;
		pcard2->temp.location = 0;
	    ///////kdiy///////
		pcard1->set_status(STATUS_ATTACK_CANCELED, TRUE);
		pcard2->set_status(STATUS_ATTACK_CANCELED, TRUE);
		++targets1->it;
		++targets2->it;
		arg.step = 0;
		return FALSE;
	}
	case 4: {
		targets1->container.insert(targets2->container.begin(), targets2->container.end());
		for(auto& pcard : targets1->container) {
			pcard->filter_disable_related_cards();
			if(pcard->unique_code && (pcard->unique_location & LOCATION_MZONE))
				add_unique_card(pcard);
			raise_single_event(pcard, nullptr, EVENT_CONTROL_CHANGED, reason_effect, REASON_EFFECT, reason_player, pcard->current.controler, 0);
			raise_single_event(pcard, nullptr, EVENT_MOVE, reason_effect, REASON_EFFECT, reason_player, pcard->current.controler, 0);
		}
		raise_event(targets1->container, EVENT_CONTROL_CHANGED, reason_effect, REASON_EFFECT, reason_player, 0, 0);
		raise_event(targets1->container, EVENT_MOVE, reason_effect, REASON_EFFECT, reason_player, 0, 0);
		process_single_event();
		process_instant_event();
		return FALSE;
	}
	case 5: {
		core.operated_set = targets1->container;
		returns.set<int32_t>(0, 1);
		return TRUE;
	}
	case 10: {
		core.operated_set.clear();
		returns.set<int32_t>(0, 0);
		return TRUE;
	}
	}
	return TRUE;
}
bool field::process(Processors::ControlAdjust& arg) {
	switch(arg.step) {
	case 0: {
		auto& destroy_set = arg.destroy_set;
		destroy_set.clear();
		uint32_t b0 = get_useable_count(nullptr, 0, LOCATION_MZONE, 0, LOCATION_REASON_CONTROL);
		uint32_t b1 = get_useable_count(nullptr, 1, LOCATION_MZONE, 1, LOCATION_REASON_CONTROL);
		for(auto& pcard : core.control_adjust_set[0])
			pcard->filter_disable_related_cards();
		for(auto& pcard : core.control_adjust_set[1])
			pcard->filter_disable_related_cards();
		if(core.control_adjust_set[0].size() > core.control_adjust_set[1].size()) {
			if(core.control_adjust_set[0].size() - core.control_adjust_set[1].size() > b1) {
				if(core.control_adjust_set[1].size() == 0 && b1 == 0) {
					destroy_set.swap(core.control_adjust_set[0]);
					arg.step = 4;
				} else {
					arg.adjusting_player = 0;
					uint32_t count = static_cast<uint32_t>(core.control_adjust_set[0].size() - core.control_adjust_set[1].size() - b1);
					core.select_cards.clear();
					for(auto& pcard : core.control_adjust_set[0])
						core.select_cards.push_back(pcard);
					auto message = pduel->new_message(MSG_HINT);
					message->write<uint8_t>(HINT_SELECTMSG);
					message->write<uint8_t>(infos.turn_player);
					message->write<uint64_t>(502);
					emplace_process<Processors::SelectCard>(1, false, count, count);
				}
			} else
				arg.step = 1;
		} else if(core.control_adjust_set[0].size() < core.control_adjust_set[1].size()) {
			if(core.control_adjust_set[1].size() - core.control_adjust_set[0].size() > b0) {
				if(core.control_adjust_set[0].size() == 0 && b0 == 0) {
					destroy_set.swap(core.control_adjust_set[1]);
					arg.step = 4;
				} else {
					arg.adjusting_player = 1;
					uint32_t count = static_cast<uint32_t>(core.control_adjust_set[1].size() - core.control_adjust_set[0].size() - b0);
					core.select_cards.clear();
					for(auto& pcard : core.control_adjust_set[1])
						core.select_cards.push_back(pcard);
					auto message = pduel->new_message(MSG_HINT);
					message->write<uint8_t>(HINT_SELECTMSG);
					message->write<uint8_t>(infos.turn_player);
					message->write<uint64_t>(502);
					emplace_process<Processors::SelectCard>(0, false, count, count);
				}
			} else
				arg.step = 1;
		} else
			arg.step = 1;
		return FALSE;
	}
	case 1: {
		auto& destroy_set = arg.destroy_set;
		destroy_set.insert(return_cards.list.begin(), return_cards.list.end());
		for(auto& pcard : return_cards.list)
			core.control_adjust_set[arg.adjusting_player].erase(pcard);
		return FALSE;
	}
	case 2: {
		for(auto& pcard : core.control_adjust_set[0]) {
			if(pcard->unique_code && (pcard->unique_location & LOCATION_MZONE))
				remove_unique_card(pcard);
		}
		for(auto& pcard : core.control_adjust_set[1]) {
			if(pcard->unique_code && (pcard->unique_location & LOCATION_MZONE))
				remove_unique_card(pcard);
		}
		auto cit1 = core.control_adjust_set[0].begin();
		auto cit2 = core.control_adjust_set[1].begin();
		while(cit1 != core.control_adjust_set[0].end() && cit2 != core.control_adjust_set[1].end()) {
			card* pcard1 = *cit1++;
			card* pcard2 = *cit2++;	
			///////kdiy///////
			uint8_t p1 = pcard1->current.controler;
			uint8_t p2 = pcard2->current.controler;
            effect* oeffect = is_player_affected_by_effect(p1,EFFECT_ORICA);
            if(oeffect && !pcard2->is_affected_by_effect(EFFECT_ORICA_SZONE)) {
                effect* deffect = pduel->new_effect();
                deffect->owner = oeffect->owner;
                deffect->code = EFFECT_ORICA_SZONE;
                deffect->type = EFFECT_TYPE_SINGLE;
                deffect->flag[0] = EFFECT_FLAG_CANNOT_DISABLE | EFFECT_FLAG_IGNORE_IMMUNE | EFFECT_FLAG_UNCOPYABLE;
                deffect->reset_flag = RESET_EVENT+0x1fe0000+RESET_CONTROL-RESET_TURN_SET;
                pcard2->add_effect(deffect);
                pcard2->reset(EFFECT_SANCT_MZONE, RESET_CODE);
            }
            if(pcard1->current.location == LOCATION_SZONE) pcard2->temp.location = LOCATION_SZONE;
            effect* oeffect2 = is_player_affected_by_effect(p2,EFFECT_ORICA);
            if(oeffect2 && !pcard1->is_affected_by_effect(EFFECT_ORICA_SZONE)) {
                effect* deffect = pduel->new_effect();
                deffect->owner = oeffect2->owner;
                deffect->code = EFFECT_ORICA_SZONE;
                deffect->type = EFFECT_TYPE_SINGLE;
                deffect->flag[0] = EFFECT_FLAG_CANNOT_DISABLE | EFFECT_FLAG_IGNORE_IMMUNE | EFFECT_FLAG_UNCOPYABLE;
                deffect->reset_flag = RESET_EVENT+0x1fe0000+RESET_CONTROL-RESET_TURN_SET;
                pcard1->add_effect(deffect);
                pcard1->reset(EFFECT_SANCT_MZONE, RESET_CODE);
            }
            if(pcard2->current.location == LOCATION_SZONE) pcard1->temp.location = LOCATION_SZONE;
	        ///////kdiy///////
			swap_card(pcard1, pcard2);
			pcard1->reset(RESET_CONTROL, RESET_EVENT);
			pcard2->reset(RESET_CONTROL, RESET_EVENT);
		    //kdiy///////
            if(oeffect && !pcard2->is_affected_by_effect(EFFECT_ORICA_SZONE)) {
                effect* deffect = pduel->new_effect();
                deffect->owner = oeffect->owner;
                deffect->code = EFFECT_ORICA_SZONE;
                deffect->type = EFFECT_TYPE_SINGLE;
                deffect->flag[0] = EFFECT_FLAG_CANNOT_DISABLE | EFFECT_FLAG_IGNORE_IMMUNE | EFFECT_FLAG_UNCOPYABLE;
                deffect->reset_flag = RESET_EVENT+0x1fe0000+RESET_CONTROL-RESET_TURN_SET;
                pcard2->add_effect(deffect);
                pcard2->reset(EFFECT_SANCT_MZONE, RESET_CODE);
            }
            if(oeffect2 && !pcard1->is_affected_by_effect(EFFECT_ORICA_SZONE)) {
                effect* deffect = pduel->new_effect();
                deffect->owner = oeffect2->owner;
                deffect->code = EFFECT_ORICA_SZONE;
                deffect->type = EFFECT_TYPE_SINGLE;
                deffect->flag[0] = EFFECT_FLAG_CANNOT_DISABLE | EFFECT_FLAG_IGNORE_IMMUNE | EFFECT_FLAG_UNCOPYABLE;
                deffect->reset_flag = RESET_EVENT+0x1fe0000+RESET_CONTROL-RESET_TURN_SET;
                pcard1->add_effect(deffect);
                pcard1->reset(EFFECT_SANCT_MZONE, RESET_CODE);
            }
			pcard1->temp.location = 0;
			pcard2->temp.location = 0;
			///////kdiy///////
		}
		auto& adjust_set = arg.adjust_set;
		adjust_set.clear();
		adjust_set.insert(cit1, core.control_adjust_set[0].end());
		adjust_set.insert(cit2, core.control_adjust_set[1].end());
		return FALSE;
	}
	case 3: {
		auto& adjust_set = arg.adjust_set;
		if(adjust_set.empty())
			return FALSE;
		auto cit = adjust_set.begin();
		card* pcard = *cit;
		adjust_set.erase(cit);
		pcard->reset(RESET_CONTROL, RESET_EVENT);
	    ///////kdiy///////
		pcard->prev_temp.location = LOCATION_MZONE;
		effect* oeffect = is_player_affected_by_effect(1 - pcard->current.controler,EFFECT_ORICA);
	    if(oeffect && !pcard->is_affected_by_effect(EFFECT_ORICA_SZONE)) {
		    effect* deffect = pduel->new_effect();
			deffect->owner = oeffect->owner;
			deffect->code = EFFECT_ORICA_SZONE;
			deffect->type = EFFECT_TYPE_SINGLE;
			deffect->flag[0] = EFFECT_FLAG_CANNOT_DISABLE | EFFECT_FLAG_IGNORE_IMMUNE | EFFECT_FLAG_UNCOPYABLE;
			deffect->reset_flag = RESET_EVENT+0x1fe0000+RESET_CONTROL-RESET_TURN_SET;
			pcard->add_effect(deffect);
            pcard->reset(EFFECT_SANCT_MZONE, RESET_CODE);
		}
		///////kdiy///////
		move_to_field(pcard, 1 - pcard->current.controler, 1 - pcard->current.controler, LOCATION_MZONE, pcard->current.position);
		arg.step = 2;
		return FALSE;
	}
	case 4: {
		core.control_adjust_set[0].insert(core.control_adjust_set[1].begin(), core.control_adjust_set[1].end());
		for(auto cit = core.control_adjust_set[0].begin(); cit != core.control_adjust_set[0].end(); ) {
			card* pcard = *cit++;
			if(!(pcard->current.location & LOCATION_ONFIELD)) {
				core.control_adjust_set[0].erase(pcard);
				continue;
			}
			pcard->filter_disable_related_cards();
			if(pcard->unique_code && (pcard->unique_location & LOCATION_MZONE))
				add_unique_card(pcard);
			raise_single_event(pcard, nullptr, EVENT_CONTROL_CHANGED, nullptr, REASON_RULE, 0, pcard->current.controler, 0);
			raise_single_event(pcard, nullptr, EVENT_MOVE, nullptr, REASON_RULE, 0, pcard->current.controler, 0);
		}
		if(core.control_adjust_set[0].size()) {
			raise_event(core.control_adjust_set[0], EVENT_CONTROL_CHANGED, nullptr, 0, 0, 0, 0);
			raise_event(core.control_adjust_set[0], EVENT_MOVE, nullptr, 0, 0, 0, 0);
		}
		process_single_event();
		process_instant_event();
		return FALSE;
	}
	case 5: {
		auto& destroy_set = arg.destroy_set;
		if(destroy_set.size())
			destroy(std::move(destroy_set), nullptr, REASON_RULE, PLAYER_NONE);
		return TRUE;
	}
	}
	return TRUE;
}
bool field::process(Processors::SelfDestroyUnique& arg) {
	auto unique_card = arg.unique_card;
	auto playerid = arg.playerid;
	switch(arg.step) {
	case 0: {
		if(core.unique_cards[playerid].find(unique_card) == core.unique_cards[playerid].end()) {
			core.unique_destroy_set.erase(unique_card);
			return TRUE;
		}
		card_set cset;
		unique_card->get_unique_target(&cset, playerid);
		if(cset.size() == 0)
			unique_card->unique_fieldid = 0;
		else if(cset.size() == 1) {
			auto cit = cset.begin();
			unique_card->unique_fieldid = (*cit)->fieldid;
		} else {
			core.select_cards.clear();
			for(auto& pcard : cset) {
				if(pcard->current.controler == playerid && pcard->unique_fieldid != UINT_MAX)
					core.select_cards.push_back(pcard);
			}
			if(core.select_cards.size() == 0) {
				playerid = 1 - playerid;
				for(auto& pcard : cset) {
					if(pcard->current.controler == playerid && pcard->unique_fieldid != UINT_MAX)
						core.select_cards.push_back(pcard);
				}
			}
			if(core.select_cards.size() == 0) {
				playerid = 1 - playerid;
				for(auto& pcard : cset) {
					if(pcard->current.controler == playerid)
						core.select_cards.push_back(pcard);
				}
			}
			if(core.select_cards.size() == 0) {
				playerid = 1 - playerid;
				for(auto& pcard : cset) {
					if(pcard->current.controler == playerid)
						core.select_cards.push_back(pcard);
				}
			}
			if(core.select_cards.size() == 1) {
				return_cards.clear();
				return_cards.list.push_back(core.select_cards.front());
			} else {
				auto message = pduel->new_message(MSG_HINT);
				message->write<uint8_t>(HINT_SELECTMSG);
				message->write<uint8_t>(playerid);
				message->write<uint64_t>(534);
				emplace_process<Processors::SelectCard>(playerid, false, 1, 1);
			}
			return FALSE;
		}
		core.unique_destroy_set.erase(unique_card);
		return TRUE;
	}
	case 1: {
		card_set cset;
		unique_card->get_unique_target(&cset, playerid);
		card* mcard = return_cards.list.front();
		unique_card->unique_fieldid = mcard->fieldid;
		cset.erase(mcard);
		for(auto& pcard : cset) {
			pcard->temp.reason_effect = pcard->current.reason_effect;
			pcard->temp.reason_player = pcard->current.reason_player;
			pcard->current.reason_effect = unique_card->unique_effect;
			pcard->current.reason_player = unique_card->current.controler;
		}
		destroy(std::move(cset), nullptr, REASON_RULE, PLAYER_SELFDES);
		return FALSE;
	}
	case 2: {
		core.unique_destroy_set.erase(unique_card);
		return TRUE;
	}
	}
	return TRUE;
}
bool field::process(Processors::SelfDestroy& arg) {
	switch(arg.step) {
	case 0: {
		if(core.self_destroy_set.empty())
			return FALSE;
		auto it = core.self_destroy_set.begin();
		card* pcard = *it;
		effect* peffect = pcard->is_affected_by_effect(EFFECT_SELF_DESTROY);
		if(peffect) {
			pcard->temp.reason_effect = pcard->current.reason_effect;
			pcard->temp.reason_player = pcard->current.reason_player;
			pcard->current.reason_effect = peffect;
			pcard->current.reason_player = peffect->get_handler_player();
			destroy(pcard, nullptr, REASON_EFFECT, PLAYER_SELFDES);
		}
		core.self_destroy_set.erase(it);
		arg.step = Processors::restart;
		return FALSE;
	}
	case 1: {
		returns.set<int32_t>(0, 0);
		core.operated_set.clear();
		return TRUE;
	}
	}
	return TRUE;
}
bool field::process(Processors::SelfToGrave& arg) {
	switch(arg.step) {
	case 0: {
		if(core.self_tograve_set.empty())
			return FALSE;
		auto it = core.self_tograve_set.begin();
		card* pcard = *it;
		effect* peffect = pcard->is_affected_by_effect(EFFECT_SELF_TOGRAVE);
		if(peffect) {
			pcard->temp.reason_effect = pcard->current.reason_effect;
			pcard->temp.reason_player = pcard->current.reason_player;
			pcard->current.reason_effect = peffect;
			pcard->current.reason_player = peffect->get_handler_player();
			send_to(pcard, nullptr, REASON_EFFECT, PLAYER_NONE, PLAYER_NONE, LOCATION_GRAVE, 0, POS_FACEUP);
		}
		core.self_tograve_set.erase(it);
		arg.step = Processors::restart;
		return FALSE;
	}
	case 1: {
		returns.set<int32_t>(0, 0);
		core.operated_set.clear();
		return TRUE;
	}
	}
	return TRUE;
}
bool field::process(Processors::TrapMonsterAdjust& arg) {
	auto& to_grave_set = arg.to_grave_set;
	auto& oppo_selection = arg.oppo_selection;
	switch(arg.step) {
	case 0: {
		if(!is_flag(DUEL_TRAP_MONSTERS_NOT_USE_ZONE)) {
			arg.step = 3;
			return FALSE;
		}
		to_grave_set.clear();
		return FALSE;
	}
	case 1: {
		uint8_t check_player = infos.turn_player;
		if(oppo_selection)
			check_player = 1 - infos.turn_player;
		refresh_location_info_instant();
		int32_t fcount = get_useable_count(nullptr, check_player, LOCATION_SZONE, check_player, 0);
		if(fcount <= 0) {
			for(auto& pcard : core.trap_monster_adjust_set[check_player]) {
				to_grave_set.insert(pcard);
			}
			core.trap_monster_adjust_set[check_player].clear();
			arg.step = 2;
		} else if((int32_t)core.trap_monster_adjust_set[check_player].size() > fcount) {
			uint32_t ct = (uint32_t)core.trap_monster_adjust_set[check_player].size() - fcount;
			core.select_cards.clear();
			for(auto& pcard : core.trap_monster_adjust_set[check_player])
				core.select_cards.push_back(pcard);
			auto message = pduel->new_message(MSG_HINT);
			message->write<uint8_t>(HINT_SELECTMSG);
			message->write<uint8_t>(check_player);
			message->write<uint64_t>(502);
			emplace_process<Processors::SelectCard>(check_player, false, ct, ct);
		} else
			arg.step = 2;
		return FALSE;
	}
	case 2: {
		uint8_t check_player = infos.turn_player;
		if(oppo_selection)
			check_player = 1 - infos.turn_player;
		for(auto& pcard : return_cards.list) {
			to_grave_set.insert(pcard);
			core.trap_monster_adjust_set[check_player].erase(pcard);
		}
		[[fallthrough]];
	}
	case 3: {
		if(!oppo_selection) {
			oppo_selection = true;
			arg.step = 0;
		}
		return FALSE;
	}
	case 4: {
		uint8_t tp = infos.turn_player;
		for(uint8_t p = 0; p < 2; ++p) {
			for(auto& pcard : core.trap_monster_adjust_set[tp]) {
				pcard->reset(RESET_TURN_SET, RESET_EVENT);
				if(!is_flag(DUEL_TRAP_MONSTERS_NOT_USE_ZONE))
					refresh_location_info_instant();
				///////kdiy///////
				pcard->prev_temp.location = LOCATION_MZONE;
				effect* seffect = is_player_affected_by_effect(tp,EFFECT_SANCT);	
				if(seffect && !pcard->is_affected_by_effect(EFFECT_SANCT_MZONE)) {
					effect* deffect = pduel->new_effect();
					deffect->owner = seffect->owner;
					deffect->code = EFFECT_SANCT_MZONE;
					deffect->type = EFFECT_TYPE_SINGLE;
					deffect->flag[0] = EFFECT_FLAG_CANNOT_DISABLE | EFFECT_FLAG_IGNORE_IMMUNE | EFFECT_FLAG_UNCOPYABLE;
					deffect->reset_flag = RESET_EVENT+0x1fe0000+RESET_CONTROL-RESET_TURN_SET;
					pcard->add_effect(deffect);
                    pcard->reset(EFFECT_ORICA_SZONE, RESET_CODE);
				}
				///////kdiy///////
				move_to_field(pcard, tp, tp, LOCATION_SZONE, pcard->current.position, FALSE, 2);		
			}
			tp = 1 - tp;
		}
		if(to_grave_set.size())
			send_to(std::move(to_grave_set), nullptr, REASON_RULE, PLAYER_NONE, PLAYER_NONE, LOCATION_GRAVE, 0, POS_FACEUP);
		return TRUE;
	}
	}
	return TRUE;
}
bool field::process(Processors::Equip& arg) {
	auto equip_player = arg.equip_player;
	auto equip_card = arg.equip_card;
	auto target = arg.target;
	auto faceup = arg.faceup;
	auto is_step = arg.is_step;
	switch(arg.step) {
	case 0: {
		returns.set<int32_t>(0, FALSE);
		if(!equip_card->is_affect_by_effect(core.reason_effect))
			return TRUE;
		if(equip_card == target)
			return TRUE;
		bool to_grave = false;
		///////////kdiy//////////			
		//if(target->current.location != LOCATION_MZONE || (target->current.position & POS_FACEDOWN)) {
		if (!((target->current.location == LOCATION_MZONE && !target->is_affected_by_effect(EFFECT_SANCT_MZONE)) || (target->current.location == LOCATION_SZONE && target->is_affected_by_effect(EFFECT_ORICA_SZONE))) || (target->current.position & POS_FACEDOWN)) {
			///////////kdiy//////////
			// if(is_flag(DUEL_EQUIP_NOT_SENT_IF_MISSING_TARGET) && equip_card->current.location == LOCATION_MZONE)
			if (is_flag(DUEL_EQUIP_NOT_SENT_IF_MISSING_TARGET) && ((equip_card->current.location == LOCATION_MZONE && !equip_card->is_affected_by_effect(EFFECT_SANCT_MZONE)) || (equip_card->current.location == LOCATION_SZONE && equip_card->is_affected_by_effect(EFFECT_ORICA_SZONE))))
				return TRUE;
			to_grave = true;
		}
		///////////kdiy//////////
		//if(equip_card->current.location != LOCATION_SZONE) {
		if(!(((equip_card->current.location == LOCATION_SZONE && !equip_card->is_affected_by_effect(EFFECT_ORICA_SZONE)) || (equip_card->current.location == LOCATION_MZONE && equip_card->is_affected_by_effect(EFFECT_SANCT_MZONE))))) {
		///////////kdiy//////////
			refresh_location_info_instant();
			if(get_useable_count(equip_card, equip_player, LOCATION_SZONE, equip_player, LOCATION_REASON_TOFIELD) <= 0)
				to_grave = true;
		}
		if(to_grave) {
			if(equip_card->current.location != LOCATION_GRAVE)
				send_to(equip_card, nullptr, REASON_RULE, PLAYER_NONE, PLAYER_NONE, LOCATION_GRAVE, 0, POS_FACEUP);
			arg.step = 2;
			return FALSE;
		}
		if(equip_card->equiping_target) {
			equip_card->effect_target_cards.erase(equip_card->equiping_target);
			equip_card->equiping_target->effect_target_owner.erase(equip_card);
			equip_card->unequip();
			equip_card->enable_field_effect(false);
			return FALSE;
		}
		///////////kdiy//////////
		//if(equip_card->current.location == LOCATION_SZONE) {
		if(((equip_card->current.location == LOCATION_SZONE && !equip_card->is_affected_by_effect(EFFECT_ORICA_SZONE)) || (equip_card->current.location == LOCATION_MZONE && equip_card->is_affected_by_effect(EFFECT_SANCT_MZONE)))) {
		///////////kdiy//////////
			if(faceup && equip_card->is_position(POS_FACEDOWN))
				change_position(equip_card, 0, equip_player, POS_FACEUP, 0);
			return FALSE;
		}
		equip_card->enable_field_effect(false);
	    equip_card->current.reason_player = equip_player;
		///////kdiy///////
		equip_card->prev_temp.location = equip_card->current.location;
		if(equip_card->current.location == LOCATION_SZONE && equip_card->is_affected_by_effect(EFFECT_ORICA_SZONE))
		    equip_card->prev_temp.location = LOCATION_MZONE;
		if(equip_card->current.location == LOCATION_MZONE && equip_card->is_affected_by_effect(EFFECT_SANCT_MZONE))
		    equip_card->prev_temp.location = LOCATION_SZONE;
		effect* seffect = is_player_affected_by_effect(equip_player,EFFECT_SANCT);
	    if(seffect && !equip_card->is_affected_by_effect(EFFECT_SANCT_MZONE)) {
		    effect* deffect = pduel->new_effect();
			deffect->owner = seffect->owner;
			deffect->code = EFFECT_SANCT_MZONE;
			deffect->type = EFFECT_TYPE_SINGLE;
			deffect->flag[0] = EFFECT_FLAG_CANNOT_DISABLE | EFFECT_FLAG_IGNORE_IMMUNE | EFFECT_FLAG_UNCOPYABLE | EFFECT_FLAG_OWNER_RELATE;
			deffect->reset_flag = RESET_EVENT+0x1fe0000-RESET_TURN_SET+RESET_CONTROL;
			equip_card->add_effect(deffect);
            equip_card->reset(EFFECT_ORICA_SZONE, RESET_CODE);
		}
		///////kdiy///////
		move_to_field(equip_card, equip_player, equip_player, LOCATION_SZONE, (faceup || equip_card->is_position(POS_FACEUP)) ? POS_FACEUP : POS_FACEDOWN);
		return FALSE;
	}
	case 1: {
		equip_card->equip(target);
		if(!(equip_card->data.type & TYPE_EQUIP)) {
			effect* peffect = pduel->new_effect();
			peffect->owner = equip_card;
			peffect->handler = equip_card;
			peffect->type = EFFECT_TYPE_SINGLE;
			if(equip_card->get_type() & TYPE_TRAP) {
				peffect->code = EFFECT_ADD_TYPE;
				peffect->value = TYPE_EQUIP;
			} else if(equip_card->data.type & TYPE_UNION) {
				peffect->code = EFFECT_CHANGE_TYPE;
				peffect->value = TYPE_EQUIP + TYPE_SPELL + TYPE_UNION;
			} else if(equip_card->data.type & TYPE_TOKEN) {
				peffect->code = EFFECT_CHANGE_TYPE;
				peffect->value = TYPE_EQUIP + TYPE_SPELL + TYPE_TOKEN;
			} else {
				peffect->code = EFFECT_CHANGE_TYPE;
				peffect->value = TYPE_EQUIP + TYPE_SPELL;
			}
			peffect->flag[0] = EFFECT_FLAG_CANNOT_DISABLE;
			peffect->reset_flag = RESET_EVENT + 0x17e0000;
			equip_card->add_effect(peffect);
		}
		equip_card->effect_target_cards.insert(target);
		target->effect_target_owner.insert(equip_card);
		if(!is_step) {
			if(equip_card->is_position(POS_FACEUP))
				equip_card->enable_field_effect(true);
			adjust_disable_check_list();
			card_set cset;
			cset.insert(equip_card);
			raise_single_event(target, &cset, EVENT_EQUIP, core.reason_effect, 0, core.reason_player, PLAYER_NONE, 0);
			raise_event(std::move(cset), EVENT_EQUIP, core.reason_effect, 0, core.reason_player, PLAYER_NONE, 0);
			core.hint_timing[target->overlay_target ? target->overlay_target->current.controler : target->current.controler] |= TIMING_EQUIP;
			process_single_event();
			process_instant_event();
			return FALSE;
		} else {
			core.equiping_cards.insert(equip_card);
			returns.set<int32_t>(0, TRUE);
			return TRUE;
		}
	}
	case 2: {
		returns.set<int32_t>(0, TRUE);
		return TRUE;
	}
	case 3: {
		returns.set<int32_t>(0, FALSE);
		return TRUE;
	}
	}
	return TRUE;
}
bool field::process(Processors::SummonRule& arg) {
	auto sumplayer = arg.sumplayer;
	auto target = arg.target;
	auto summon_procedure_effect = arg.summon_procedure_effect;
	auto ignore_count = arg.ignore_count;
	auto min_tribute = arg.min_tribute;
	auto zone = arg.zone;
	switch(arg.step) {
	case 0: {
		if(!target->is_summonable_card())
			return TRUE;
		if(check_unique_onfield(target, sumplayer, LOCATION_MZONE))
			return TRUE;
		if(target->is_affected_by_effect(EFFECT_CANNOT_SUMMON))
			return TRUE;
		///////////kdiy//////////			
		//if(target->current.location == LOCATION_MZONE) {
		if((target->current.location == LOCATION_MZONE && !target->is_affected_by_effect(EFFECT_SANCT_MZONE)) || (target->current.location == LOCATION_SZONE && target->is_affected_by_effect(EFFECT_ORICA_SZONE))) {
		///////////kdiy//////////				
			if(target->is_position(POS_FACEDOWN))
				return TRUE;
			if(!ignore_count && (core.extra_summon[sumplayer] || !target->is_affected_by_effect(EFFECT_EXTRA_SUMMON_COUNT))
				&& (core.summon_count[sumplayer] >= get_summon_count_limit(sumplayer)))
				return TRUE;
			if(!target->is_affected_by_effect(EFFECT_GEMINI_SUMMONABLE))
				return TRUE;
			if(target->is_affected_by_effect(EFFECT_GEMINI_STATUS))
				return TRUE;
			if(!is_player_can_summon(SUMMON_TYPE_GEMINI, sumplayer, target, sumplayer))
				return TRUE;
		} else {
			effect_set eset;
			int32_t res = target->filter_summon_procedure(sumplayer, &eset, ignore_count, min_tribute, zone);
			if(summon_procedure_effect) {
				if(res < 0 || !target->check_summon_procedure(summon_procedure_effect, sumplayer, ignore_count, min_tribute, zone))
					return TRUE;
			} else {
				if(res == -2)
					return TRUE;
				core.select_effects.clear();
				core.select_options.clear();
				if(res > 0) {
					core.select_effects.push_back(nullptr);
					core.select_options.push_back(1);
				}
				for(const auto& peff : eset) {
					core.select_effects.push_back(peff);
					core.select_options.push_back(peff->description);
				}
				if(core.select_options.empty())
					return TRUE;
				if(core.select_options.size() == 1)
					returns.set<int32_t>(0, 0);
				else
					emplace_process<Processors::SelectOption>(sumplayer);
			}
		}
		if(core.summon_depth)
			core.summon_cancelable = FALSE;
		++core.summon_depth;
		target->material_cards.clear();
		return FALSE;
	}
	case 1: {
		effect_set eset;
		target->filter_effect(EFFECT_EXTRA_SUMMON_COUNT, &eset);
		///////////kdiy//////////
		//if(target->current.location == LOCATION_MZONE) {
		if((target->current.location == LOCATION_MZONE && !target->is_affected_by_effect(EFFECT_SANCT_MZONE)) || (target->current.location == LOCATION_SZONE && target->is_affected_by_effect(EFFECT_ORICA_SZONE))) {
		///////////kdiy//////////
			arg.step = 4;
			if(!ignore_count && !core.extra_summon[sumplayer]) {
				if(!eset.empty()) {
					arg.extra_summon_effect = eset.front();
					return FALSE;
				}
			}
			arg.extra_summon_effect = nullptr;
			return FALSE;
		}
		if(!summon_procedure_effect) {
			summon_procedure_effect = core.select_effects[returns.at<int32_t>(0)];
			arg.summon_procedure_effect = summon_procedure_effect;
		}
		core.select_effects.clear();
		core.select_options.clear();
		if(ignore_count || core.summon_count[sumplayer] < get_summon_count_limit(sumplayer)) {
			core.select_effects.push_back(nullptr);
			core.select_options.push_back(1);
		}
		if(!ignore_count && !core.extra_summon[sumplayer]) {
			for(const auto& peff : eset) {
				std::vector<lua_Integer> retval;
				peff->get_value(target, 0, retval);
				uint8_t new_min_tribute = retval.size() > 0 ? static_cast<uint8_t>(retval[0]) : 0;
				uint32_t new_zone = retval.size() > 1 ? static_cast<uint32_t>(retval[1]) : 0x1f001f;
				///////kdiy///////
				if(is_player_affected_by_effect(sumplayer,EFFECT_ORICA) && retval.size()<2)
				    new_zone+= 0x1f00;
				if(is_player_affected_by_effect(1-sumplayer,EFFECT_ORICA) && retval.size()<2)
				    new_zone+= 0x1f0000;
				///////kdiy///////
				uint32_t releasable = 0xff00ffu;
				if(retval.size() > 2) {
					if(retval[2] < 0)
						releasable += static_cast<int32_t>(retval[2]);
					else
						releasable = static_cast<uint32_t>(retval[2]);
				}
				///////kdiy///////
				if(is_player_affected_by_effect(sumplayer,EFFECT_ORICA)) {
					if(retval.size() < 0 || retval.size() < 3)
					   releasable+= 0x1f00;
				}
				if(is_player_affected_by_effect(1-sumplayer,EFFECT_ORICA)) {
					if(retval.size() < 0 || retval.size() < 3)
					   releasable+= 0x1f000000;
				}
				///////kdiy///////
				if (summon_procedure_effect && summon_procedure_effect->is_flag(EFFECT_FLAG_SPSUM_PARAM) && summon_procedure_effect->o_range)
					new_zone = (new_zone >> 16) | (new_zone & 0xffffu << 16);
				new_zone &= zone;
				if(summon_procedure_effect) {
					if(new_min_tribute < min_tribute)
						new_min_tribute = min_tribute;
					if(!target->is_summonable(summon_procedure_effect, new_min_tribute, new_zone, releasable, peff))
						continue;
				} else {
					int32_t rcount = target->get_summon_tribute_count();
					int32_t min = rcount & 0xffff;
					int32_t max = (rcount >> 16) & 0xffff;
					if(!is_player_can_summon(SUMMON_TYPE_ADVANCE, sumplayer, target, sumplayer))
						max = 0;
					if(min < min_tribute)
						min = min_tribute;
					if(max < min)
						continue;
					if(min < new_min_tribute)
						min = new_min_tribute;
					if(!check_tribute(target, min, max, nullptr, target->current.controler, new_zone, releasable))
						continue;
				}
				core.select_effects.push_back(peff);
				core.select_options.push_back(peff->description);
			}
		}
		if(core.select_options.size() == 1)
			returns.set<int32_t>(0, 0);
		else
			emplace_process<Processors::SelectOption>(sumplayer);
		return FALSE;
	}
	case 2: {
		effect* pextra = core.select_effects[returns.at<int32_t>(0)];
		arg.extra_summon_effect = pextra;
		uint32_t releasable = 0xff00ffu;
		///////kdiy///////
		if(is_player_affected_by_effect(sumplayer,EFFECT_ORICA)) {
			releasable+= 0x1f00;  
		}
		if(is_player_affected_by_effect(1-sumplayer,EFFECT_ORICA)) {
			releasable+= 0x1f000000; 
		}
		///////kdiy///////
		if(pextra) {
			std::vector<lua_Integer> retval;
			pextra->get_value(target, 0, retval);
			uint8_t new_min_tribute = retval.size() > 0 ? static_cast<uint8_t>(retval[0]) : 0;
			uint32_t new_zone = retval.size() > 1 ? static_cast<uint32_t>(retval[1]) : 0x1f001f;
			///////kdiy///////
			if(is_player_affected_by_effect(sumplayer,EFFECT_ORICA) && retval.size()<2)
				new_zone+= 0x1f00;
			if(is_player_affected_by_effect(1-sumplayer,EFFECT_ORICA) && retval.size()<2)
				new_zone+= 0x1f0000;				  
			///////kdiy///////
			if(retval.size() > 2) {
				if(retval[2] < 0)
					releasable += static_cast<int32_t>(retval[2]);
				else
					releasable = static_cast<uint32_t>(retval[2]);
			}
			///////kdiy///////
			if(is_player_affected_by_effect(sumplayer,EFFECT_ORICA)) {
				if(retval.size() < 0 || retval.size() < 3)
					releasable+= 0x1f00;  
			}
			if(is_player_affected_by_effect(1-sumplayer,EFFECT_ORICA)) {
				if(retval.size() < 0 || retval.size() < 3)
					releasable+= 0x1f000000; 
			}
			///////kdiy///////
			if(min_tribute < new_min_tribute)
				arg.min_tribute = new_min_tribute;
			if (summon_procedure_effect && summon_procedure_effect->is_flag(EFFECT_FLAG_SPSUM_PARAM) && summon_procedure_effect->o_range)
					new_zone = (new_zone >> 16) | (new_zone & 0xffffu << 16);
			arg.zone &= new_zone;
		}
		if(summon_procedure_effect) {
			arg.step = 3;
			return FALSE;
		}
		core.select_cards.clear();
		int32_t required = target->get_summon_tribute_count();
		int32_t min = required & 0xffff;
		int32_t max = required >> 16;
		if(min < min_tribute) {
			min = min_tribute;
		}
		uint32_t adv = is_player_can_summon(SUMMON_TYPE_ADVANCE, sumplayer, target, sumplayer);
		if(max == 0 || !adv) {
			return_cards.clear();
			arg.step = 4;
		} else {
			core.release_cards.clear();
			core.release_cards_ex.clear();
			core.release_cards_ex_oneof.clear();
			int32_t rcount = get_summon_release_list(target, &core.release_cards, &core.release_cards_ex, &core.release_cards_ex_oneof, nullptr, 0, releasable);
			if(rcount == 0) {
				return_cards.clear();
				arg.step = 4;
			} else {
				int32_t ct = get_tofield_count(target, sumplayer, LOCATION_MZONE, sumplayer, LOCATION_REASON_TOFIELD, zone);
				int32_t fcount = get_mzone_limit(sumplayer, sumplayer, LOCATION_REASON_TOFIELD);
				////////kdiy/////
				if(is_player_affected_by_effect(sumplayer,EFFECT_ORICA))
				  fcount += get_szone_limit(sumplayer, sumplayer, LOCATION_REASON_TOFIELD);
				////////kdiy/////					
				if(min == 0 && ct > 0 && fcount > 0) {
					emplace_process<Processors::SelectYesNo>(sumplayer, 90);
					arg.max_allowed_tributes = max;
				} else {
					if(min < -fcount + 1) {
						min = -fcount + 1;
					}
					select_tribute_cards(target, sumplayer, core.summon_cancelable, min, max, sumplayer, zone);
					arg.step = 4;
				}
			}
		}
		return FALSE;
	}
	case 3: {
		if(returns.at<int32_t>(0))
			return_cards.clear();
		else {
			int32_t max = arg.max_allowed_tributes;
			select_tribute_cards(target, sumplayer, core.summon_cancelable, 1, max, sumplayer, zone);
		}
		arg.step = 4;
		return FALSE;
	}
	case 4: {
		returns.set<int32_t>(0, TRUE);
		if(summon_procedure_effect->target) {
			auto pextra = arg.extra_summon_effect;
			uint32_t releasable = 0xff00ffu;
			///////kdiy///////
			if(is_player_affected_by_effect(sumplayer,EFFECT_ORICA))
			    releasable+= 0x1f00;
			if(is_player_affected_by_effect(1-sumplayer,EFFECT_ORICA))
			    releasable+= 0x1f000000;				
			///////kdiy///////
			if(pextra) {
				std::vector<lua_Integer> retval;
				pextra->get_value(target, 0, retval);
				if(retval.size() > 2) {
					if(retval[2] < 0)
						releasable += static_cast<int32_t>(retval[2]);
					else
						releasable = static_cast<uint32_t>(retval[2]);
				}
				///////kdiy///////
				if(is_player_affected_by_effect(sumplayer,EFFECT_ORICA)) {
					if(retval.size() < 0 || retval.size() < 3)
					   releasable+= 0x1f00;  
				}
				if(is_player_affected_by_effect(1-sumplayer,EFFECT_ORICA)) {
					if(retval.size() < 0 || retval.size() < 3)
					   releasable+= 0x1f000000; 
			    }
			    ///////kdiy///////
			}
			pduel->lua->add_param<LuaParam::CARD>(target);
			pduel->lua->add_param<LuaParam::INT>(min_tribute);
			pduel->lua->add_param<LuaParam::INT>(zone);
			pduel->lua->add_param<LuaParam::INT>(releasable);
			pduel->lua->add_param<LuaParam::EFFECT>(pextra);
			core.sub_solving_event.push_back(nil_event);
			emplace_process<Processors::ExecuteTarget>(summon_procedure_effect, sumplayer);
		}
		return FALSE;
	}
	case 5: {
		if(target->current.location == LOCATION_MZONE)
			arg.step = 9;
		else if(summon_procedure_effect) {
			if(!returns.at<int32_t>(0)) {
				--core.summon_depth;
				return TRUE;
			}
			arg.step = 6;
		}
		else {
			if(return_cards.canceled) {
				--core.summon_depth;
				return TRUE;
			}
			arg.tributes.clear();
			arg.tributes.insert(return_cards.list.begin(), return_cards.list.end());
		}
		arg.summon_cost_effects.clear();
		target->filter_effect(EFFECT_SUMMON_COST, &arg.summon_cost_effects);
		for(const auto& peffect : arg.summon_cost_effects) {
			if(peffect->operation) {
				core.sub_solving_event.push_back(nil_event);
				emplace_process<Processors::ExecuteOperation>(peffect, sumplayer);
			}
		}
		return FALSE;
	}
	case 6: {
		auto& tributes = arg.tributes;
		int32_t min = 0;
		int32_t level = target->get_level();
		if(level < 5)
			min = 0;
		else if(level < 7)
			min = 1;
		else
			min = 2;
		min -= static_cast<int32_t>(tributes.size());
		if(min > 0) {
			effect_set eset;
			target->filter_effect(EFFECT_DECREASE_TRIBUTE, &eset);
			int32_t minul = 0;
			effect* pdec = nullptr;
			for(const auto& peff : eset) {
				if(!peff->is_flag(EFFECT_FLAG_COUNT_LIMIT)) {
					int32_t dec = peff->get_value(target);
					if(minul < (dec & 0xffff)) {
						minul = dec & 0xffff;
						pdec = peff;
					}
				}
			}
			if(pdec) {
				min -= minul;
				auto message = pduel->new_message(MSG_HINT);
				message->write<uint8_t>(HINT_CARD);
				message->write<uint8_t>(0);
				message->write<uint64_t>(pdec->handler->data.code);
			}
			for(const auto& peffect : eset) {
				if(peffect->is_flag(EFFECT_FLAG_COUNT_LIMIT) && peffect->count_limit > 0 && peffect->target) {
					int32_t dec = peffect->get_value(target);
					min -= dec & 0xffff;
					peffect->dec_count();
					auto message = pduel->new_message(MSG_HINT);
					message->write<uint8_t>(HINT_CARD);
					message->write<uint8_t>(0);
					message->write<uint64_t>(peffect->handler->data.code);
					if(min <= 0)
						break;
				}
			}
			for(const auto& peffect : eset) {
				if(peffect->is_flag(EFFECT_FLAG_COUNT_LIMIT) && peffect->count_limit > 0 && !peffect->target) {
					int32_t dec = peffect->get_value(target);
					min -= dec & 0xffff;
					peffect->dec_count();
					auto message = pduel->new_message(MSG_HINT);
					message->write<uint8_t>(HINT_CARD);
					message->write<uint8_t>(0);
					message->write<uint64_t>(peffect->handler->data.code);
					if(min <= 0)
						break;
				}
			}
		}
		target->summon.location = LOCATION_HAND;
		target->summon.type = SUMMON_TYPE_NORMAL;
		target->summon.pzone = false;
		if(!tributes.empty()) {
			for(auto& pcard : tributes)
				pcard->current.reason_card = target;
			target->set_material(tributes);
			release(std::move(tributes), nullptr, REASON_SUMMON | REASON_MATERIAL, sumplayer);
			target->summon.type |= SUMMON_TYPE_ADVANCE;
			adjust_all();
		}
		target->current.reason_effect = nullptr;
		target->current.reason_player = sumplayer;
		arg.step = 7;
		return FALSE;
	}
	case 7: {
		target->summon.location = LOCATION_HAND;
		target->summon.pzone = false;
		target->summon.type = (summon_procedure_effect->get_value(target) & 0xfffffff) | SUMMON_TYPE_NORMAL;
		target->current.reason_effect = summon_procedure_effect;
		target->current.reason_player = sumplayer;
		returns.set<int32_t>(0, TRUE);
		if(summon_procedure_effect->operation) {
			auto pextra = arg.extra_summon_effect;
			uint32_t releasable = 0xff00ffu;
			if(pextra) {
				std::vector<lua_Integer> retval;
				pextra->get_value(target, 0, retval);
				if(retval.size() > 2) {
					if(retval[2] < 0)
						releasable += static_cast<int32_t>(retval[2]);
					else
						releasable = static_cast<uint32_t>(retval[2]);
				}
			}
			pduel->lua->add_param<LuaParam::CARD>(target);
			pduel->lua->add_param<LuaParam::INT>(min_tribute);
			pduel->lua->add_param<LuaParam::INT>(zone);
			pduel->lua->add_param<LuaParam::INT>(releasable);
			pduel->lua->add_param<LuaParam::EFFECT>(pextra);
			core.sub_solving_event.push_back(nil_event);
			emplace_process<Processors::ExecuteOperation>(summon_procedure_effect, sumplayer);
		}
		summon_procedure_effect->dec_count(sumplayer);
		return FALSE;
	}
	case 8: {
		--core.summon_depth;
		if(core.summon_depth)
			return TRUE;
		break_effect();
		if(ignore_count)
			return FALSE;
		auto pextra = arg.extra_summon_effect;
		if(!pextra)
			++core.summon_count[sumplayer];
		else {
			core.extra_summon[sumplayer] = TRUE;
			auto message = pduel->new_message(MSG_HINT);
			message->write<uint8_t>(HINT_CARD);
			message->write<uint8_t>(0);
			message->write<uint64_t>(pextra->handler->data.code);
			if(pextra->operation) {
				pduel->lua->add_param<LuaParam::CARD>(target);
				core.sub_solving_event.push_back(nil_event);
				emplace_process<Processors::ExecuteOperation>(pextra, sumplayer);
			}
		}
		return FALSE;
	}
	case 9: {
		uint8_t targetplayer = sumplayer;
		uint8_t positions = POS_FACEUP_ATTACK;
		if(is_flag(DUEL_NORMAL_SUMMON_FACEUP_DEF) || is_player_affected_by_effect(sumplayer, EFFECT_DEVINE_LIGHT))
			positions = POS_FACEUP;
		if(summon_procedure_effect && summon_procedure_effect->is_flag(EFFECT_FLAG_SPSUM_PARAM)) {
			positions = (uint8_t)summon_procedure_effect->s_range & POS_FACEUP;
			if(summon_procedure_effect->o_range)
				targetplayer = 1 - sumplayer;
		}
		effect_set eset;
		filter_player_effect(sumplayer, EFFECT_FORCE_NORMAL_SUMMON_POSITION, &eset);
		for(auto& eff : eset) {
			if(eff->target) {
				pduel->lua->add_param<LuaParam::EFFECT>(eff);
				pduel->lua->add_param<LuaParam::CARD>(target);
				pduel->lua->add_param<LuaParam::INT>(sumplayer);
				pduel->lua->add_param<LuaParam::INT>(target->summon.type);
				pduel->lua->add_param<LuaParam::INT>(positions);
				pduel->lua->add_param<LuaParam::INT>(targetplayer);
				if(!pduel->lua->check_condition(eff->target, 6))
					continue;
			}
			positions &= eff->get_value();
		}
		target->enable_field_effect(false);
	    ///////kdiy///////
		target->prev_temp.location = target->current.location;
		if(target->current.location == LOCATION_SZONE && target->is_affected_by_effect(EFFECT_ORICA_SZONE))
		    target->prev_temp.location = LOCATION_MZONE;
		if(target->current.location == LOCATION_MZONE && target->is_affected_by_effect(EFFECT_SANCT_MZONE))
		    target->prev_temp.location = LOCATION_SZONE;
		effect* oeffect = is_player_affected_by_effect(targetplayer,EFFECT_ORICA);
	    if(oeffect && !target->is_affected_by_effect(EFFECT_ORICA_SZONE)) {
		    effect* deffect = pduel->new_effect();
			deffect->owner = oeffect->owner;
			deffect->code = EFFECT_ORICA_SZONE;
			deffect->type = EFFECT_TYPE_SINGLE;
			deffect->flag[0] = EFFECT_FLAG_CANNOT_DISABLE | EFFECT_FLAG_IGNORE_IMMUNE | EFFECT_FLAG_UNCOPYABLE;
			deffect->reset_flag = RESET_EVENT+0x1fe0000+RESET_CONTROL-RESET_TURN_SET;
			target->add_effect(deffect);
            target->reset(EFFECT_SANCT_MZONE, RESET_CODE);
		}
		///////kdiy///////
		move_to_field(target, sumplayer, targetplayer, LOCATION_MZONE, positions, FALSE, 0, zone);
		arg.step = 11;
		return FALSE;
	}
	case 10: {
		//gemini handling
		--core.summon_depth;
		if(core.summon_depth)
			return TRUE;
		target->enable_field_effect(false);
		target->summon.location = LOCATION_MZONE;
		target->summon.pzone = false;
		target->summon.sequence = target->current.sequence;
		target->summon.type |= SUMMON_TYPE_NORMAL;
		target->current.reason_effect = nullptr;
		target->current.reason_player = sumplayer;
		effect* deffect = pduel->new_effect();
		deffect->owner = target;
		deffect->code = EFFECT_GEMINI_STATUS;
		deffect->type = EFFECT_TYPE_SINGLE;
		deffect->flag[0] = EFFECT_FLAG_CANNOT_DISABLE | EFFECT_FLAG_CLIENT_HINT;
		deffect->description = 64;
		deffect->reset_flag = RESET_EVENT + 0x1fe0000;
		target->add_effect(deffect);
		return FALSE;
	}
	case 11: {
		if(ignore_count)
			return FALSE;
		auto pextra = arg.extra_summon_effect;
		if(!pextra)
			++core.summon_count[sumplayer];
		else {
			core.extra_summon[sumplayer] = TRUE;
			auto message = pduel->new_message(MSG_HINT);
			message->write<uint8_t>(HINT_CARD);
			message->write<uint8_t>(0);
			message->write<uint64_t>(pextra->handler->data.code);
			if(pextra->operation) {
				pduel->lua->add_param<LuaParam::CARD>(target);
				core.sub_solving_event.push_back(nil_event);
				emplace_process<Processors::ExecuteOperation>(pextra, sumplayer);
			}
		}
		return FALSE;
	}
	case 12: {
		set_control(target, target->current.controler, 0, 0);
		core.phase_action = true;
		target->current.reason = REASON_SUMMON;
		target->summon.player = sumplayer;
		auto message = pduel->new_message(MSG_SUMMONING);
		message->write<uint32_t>(target->data.code);
		message->write(target->get_info_location());
        ///kdiy///////
        message->write<uint8_t>(sumplayer);
        ///kdiy///////
		if(is_flag(DUEL_CANNOT_SUMMON_OATH_OLD)) {
			++core.summon_state_count[sumplayer];
			++core.normalsummon_state_count[sumplayer];
			check_card_counter(target, ACTIVITY_SUMMON, sumplayer);
			check_card_counter(target, ACTIVITY_NORMALSUMMON, sumplayer);
		}
		if (target->material_cards.size()) {
			for (auto& mcard : target->material_cards)
				raise_single_event(mcard, nullptr, EVENT_BE_PRE_MATERIAL, summon_procedure_effect, REASON_SUMMON, sumplayer, sumplayer, 0);
			raise_event(target->material_cards, EVENT_BE_PRE_MATERIAL, summon_procedure_effect, REASON_SUMMON, sumplayer, sumplayer, 0);
		}
		process_single_event();
		process_instant_event();
		return FALSE;
	}
	case 13: {
		if(core.current_chain.size() == 0) {
			if(target->is_affected_by_effect(EFFECT_CANNOT_DISABLE_SUMMON))
				arg.step = 15;
			return FALSE;
		} else {
			arg.step = 15;
			return FALSE;
		}
		return FALSE;
	}
	case 14: {
		target->set_status(STATUS_SUMMONING, TRUE);
		target->set_status(STATUS_SUMMON_DISABLED, FALSE);
		raise_event(target, EVENT_SUMMON, summon_procedure_effect, 0, sumplayer, sumplayer, 0);
		process_instant_event();
		emplace_process<Processors::PointEvent>(true, true, true);
		return FALSE;
	}
	case 15: {
		if(target->is_status(STATUS_SUMMONING)) {
			arg.step = 15;
			return FALSE;
		}
		if(summon_procedure_effect && !is_flag(DUEL_CANNOT_SUMMON_OATH_OLD)) {
			remove_oath_effect(summon_procedure_effect);
			if(summon_procedure_effect->is_flag(EFFECT_FLAG_COUNT_LIMIT) && (summon_procedure_effect->count_flag & EFFECT_COUNT_CODE_OATH)) {
				dec_effect_code(summon_procedure_effect->count_code, summon_procedure_effect->count_flag, summon_procedure_effect->count_hopt_index, sumplayer);
			}
		}
		for(const auto& peffect : arg.summon_cost_effects)
			remove_oath_effect(peffect);
		///////////kdiy//////////
		//if(target->current.location == LOCATION_MZONE)
		if(target->current.location == LOCATION_MZONE || (target->current.location == LOCATION_SZONE && target->is_affected_by_effect(EFFECT_ORICA_SZONE)))
		///////////kdiy//////////
			send_to(target, nullptr, REASON_RULE, sumplayer, sumplayer, LOCATION_GRAVE, 0, 0);
		adjust_instant();
		emplace_process<Processors::PointEvent>(false, false, false);
		return TRUE;
	}
	case 16: {
		if(summon_procedure_effect) {
			release_oath_relation(summon_procedure_effect);
		}
		for(const auto& peffect : arg.summon_cost_effects)
			release_oath_relation(peffect);
		target->set_status(STATUS_SUMMONING, FALSE);
		target->set_status(STATUS_SUMMON_TURN, TRUE);
		target->enable_field_effect(true);
		if(target->is_status(STATUS_DISABLED))
			target->reset(RESET_DISABLE, RESET_EVENT);
		return FALSE;
	}
	case 17: {
		pduel->new_message(MSG_SUMMONED);
		adjust_instant();
		if(target->material_cards.size()) {
			for(auto& mcard : target->material_cards)
				raise_single_event(mcard, nullptr, EVENT_BE_MATERIAL, summon_procedure_effect, REASON_SUMMON, sumplayer, sumplayer, 0);
			raise_event(target->material_cards, EVENT_BE_MATERIAL, summon_procedure_effect, REASON_SUMMON, sumplayer, sumplayer, 0);
		}
		process_single_event();
		process_instant_event();
		return FALSE;
	}
	case 18: {
		if(!is_flag(DUEL_CANNOT_SUMMON_OATH_OLD)) {
			++core.summon_state_count[sumplayer];
			++core.normalsummon_state_count[sumplayer];
			check_card_counter(target, ACTIVITY_SUMMON, sumplayer);
			check_card_counter(target, ACTIVITY_NORMALSUMMON, sumplayer);
		}
		raise_single_event(target, nullptr, EVENT_SUMMON_SUCCESS, summon_procedure_effect, 0, sumplayer, sumplayer, 0);
		process_single_event();
		raise_event(target, EVENT_SUMMON_SUCCESS, summon_procedure_effect, 0, sumplayer, sumplayer, 0);
		process_instant_event();
		if(core.current_chain.size() == 0) {
			adjust_all();
			core.hint_timing[sumplayer] |= TIMING_SUMMON;
			emplace_process<Processors::PointEvent>(false, false, false);
		}
		return TRUE;
	}
	}
	return TRUE;
}
bool field::process(Processors::FlipSummon& arg) {
	auto sumplayer = arg.sumplayer;
	auto target = arg.target;
	switch(arg.step) {
	case 0: {
		///////////kdiy//////////				
		//if(target->current.location != LOCATION_MZONE)
		if(!((target->current.location == LOCATION_MZONE && !target->is_affected_by_effect(EFFECT_SANCT_MZONE)) || (target->current.location == LOCATION_SZONE && target->is_affected_by_effect(EFFECT_ORICA_SZONE))))
		///////////kdiy//////////				
			return TRUE;
		if(!(target->current.position & POS_FACEDOWN))
			return TRUE;
		if(check_unique_onfield(target, sumplayer, LOCATION_MZONE))
			return TRUE;
		auto& summon_cost_effects = arg.flip_summon_cost_effects;
		summon_cost_effects.clear();
		target->filter_effect(EFFECT_FLIPSUMMON_COST, &summon_cost_effects);
		for(const auto& peffect : summon_cost_effects) {
			if(peffect->operation) {
				core.sub_solving_event.push_back(nil_event);
				emplace_process<Processors::ExecuteOperation>(peffect, sumplayer);
			}
		}
		return FALSE;
	}
	case 1: {
		target->previous.position = target->current.position;
		target->current.position = POS_FACEUP_ATTACK;
		target->summon.player = sumplayer;
		target->fieldid = infos.field_id++;
		core.phase_action = true;
		if(is_flag(DUEL_CANNOT_SUMMON_OATH_OLD)) {
			++core.flipsummon_state_count[sumplayer];
			check_card_counter(target, ACTIVITY_FLIPSUMMON, sumplayer);
		}
		auto message = pduel->new_message(MSG_FLIPSUMMONING);
		message->write<uint32_t>(target->data.code);
		message->write(target->get_info_location());
        ///kdiy///////
        message->write<uint8_t>(sumplayer);
        ///kdiy///////
		if(target->is_affected_by_effect(EFFECT_CANNOT_DISABLE_FLIP_SUMMON))
			arg.step = 2;
		else {
			target->set_status(STATUS_SUMMONING, TRUE);
			target->set_status(STATUS_SUMMON_DISABLED, FALSE);
			raise_event(target, EVENT_FLIP_SUMMON, nullptr, 0, sumplayer, sumplayer, 0);
			process_instant_event();
			emplace_process<Processors::PointEvent>(true, true, true);
		}
		return FALSE;
	}
	case 2: {
		if(target->is_status(STATUS_SUMMONING))
			return FALSE;
		for(const auto& peffect : arg.flip_summon_cost_effects)
			remove_oath_effect(peffect);
		///////////kdiy//////////
		//if(target->current.location == LOCATION_MZONE)
		if((target->current.location == LOCATION_MZONE && !target->is_affected_by_effect(EFFECT_SANCT_MZONE)) || (target->current.location == LOCATION_SZONE && target->is_affected_by_effect(EFFECT_ORICA_SZONE)))
		///////////kdiy//////////
			send_to(target, nullptr, REASON_RULE, sumplayer, sumplayer, LOCATION_GRAVE, 0, 0);
		emplace_process<Processors::PointEvent>(false, false, false);
		return TRUE;
	}
	case 3: {
		for(const auto& peffect : arg.flip_summon_cost_effects)
			release_oath_relation(peffect);
		target->set_status(STATUS_SUMMONING, FALSE);
		target->enable_field_effect(true);
		if(target->is_status(STATUS_DISABLED))
			target->reset(RESET_DISABLE, RESET_EVENT);
		target->set_status(STATUS_FLIP_SUMMON_TURN, TRUE);
		return FALSE;
	}
	case 4: {
		pduel->new_message(MSG_FLIPSUMMONED);
		if(!is_flag(DUEL_CANNOT_SUMMON_OATH_OLD)) {
			++core.flipsummon_state_count[sumplayer];
			check_card_counter(target, ACTIVITY_FLIPSUMMON, sumplayer);
		}
		adjust_instant();
		raise_single_event(target, nullptr, EVENT_FLIP, nullptr, 0, sumplayer, sumplayer, 0);
		raise_single_event(target, nullptr, EVENT_FLIP_SUMMON_SUCCESS, nullptr, 0, sumplayer, sumplayer, 0);
		raise_single_event(target, nullptr, EVENT_CHANGE_POS, nullptr, 0, sumplayer, sumplayer, 0);
		process_single_event();
		raise_event(target, EVENT_FLIP, nullptr, 0, sumplayer, sumplayer, 0);
		raise_event(target, EVENT_FLIP_SUMMON_SUCCESS, nullptr, 0, sumplayer, sumplayer, 0);
		raise_event(target, EVENT_CHANGE_POS, nullptr, 0, sumplayer, sumplayer, 0);
		process_instant_event();
		adjust_all();
		if(core.current_chain.size() == 0) {
			core.hint_timing[sumplayer] |= TIMING_FLIPSUMMON;
			emplace_process<Processors::PointEvent>(false, false, false);
		}
		return TRUE;
	}
	}
	return TRUE;
}
bool field::process(Processors::MonsterSet& arg) {
	auto setplayer = arg.setplayer;
	auto target = arg.target;
	auto summon_procedure_effect = arg.summon_procedure_effect;
	auto ignore_count = arg.ignore_count;
	auto min_tribute = arg.min_tribute;
	auto zone = arg.zone;
	switch(arg.step) {
	case 0: {
		if(target->is_affected_by_effect(EFFECT_UNSUMMONABLE_CARD))
			return TRUE;
		if(target->current.location != LOCATION_HAND)
			return TRUE;
		if(!(target->data.type & TYPE_MONSTER))
			return TRUE;
		if(target->is_affected_by_effect(EFFECT_CANNOT_MSET))
			return TRUE;
		effect_set eset;					
		int32_t res = target->filter_set_procedure(setplayer, &eset, ignore_count, min_tribute, zone);
		if(summon_procedure_effect) {
			if(res < 0 || !target->check_set_procedure(summon_procedure_effect, setplayer, ignore_count, min_tribute, zone))
				return TRUE;
		} else {
			if(res == -2)
				return TRUE;
			core.select_effects.clear();
			core.select_options.clear();
			if(res > 0) {
				core.select_effects.push_back(nullptr);
				core.select_options.push_back(1);
			}
			for(const auto& peff : eset) {
				core.select_effects.push_back(peff);
				core.select_options.push_back(peff->description);
			}
			if(core.select_options.size() == 1)
				returns.set<int32_t>(0, 0);
			else
				emplace_process<Processors::SelectOption>(setplayer);
		}
		target->material_cards.clear();
		return FALSE;
	}
	case 1: {
		effect_set eset;
		target->filter_effect(EFFECT_EXTRA_SET_COUNT, &eset);
		if(!summon_procedure_effect) {
			summon_procedure_effect = core.select_effects[returns.at<int32_t>(0)];
			arg.summon_procedure_effect = summon_procedure_effect;
		}
		core.select_effects.clear();
		core.select_options.clear();
		if(ignore_count || core.summon_count[setplayer] < get_summon_count_limit(setplayer)) {
			core.select_effects.push_back(nullptr);
			core.select_options.push_back(1);
		}
		if(!ignore_count && !core.extra_summon[setplayer]) {
			for(const auto& peff : eset) {
				std::vector<lua_Integer> retval;
				peff->get_value(target, 0, retval);
				uint8_t new_min_tribute = retval.size() > 0 ? static_cast<uint8_t>(retval[0]) : 0;
				uint32_t new_zone = retval.size() > 1 ? static_cast<uint32_t>(retval[1]) : 0x1f001f;
				///////kdiy///////
				if(is_player_affected_by_effect(setplayer,EFFECT_ORICA) && retval.size()<2)
				    new_zone+= 0x1f00;
				if(is_player_affected_by_effect(1-setplayer,EFFECT_ORICA) && retval.size()<2)
				    new_zone+= 0x1f0000;				  
				///////kdiy///////
				uint32_t releasable = 0xff00ffu;
				if(retval.size() > 2) {
					if(retval[2] < 0)
						releasable += static_cast<int32_t>(retval[2]);
					else
						releasable = static_cast<uint32_t>(retval[2]);
				}
				///////kdiy///////
				if(is_player_affected_by_effect(setplayer,EFFECT_ORICA)) {
					if(retval.size() < 0 || retval.size() < 3)
					   releasable+= 0x1f00;
				}
				if(is_player_affected_by_effect(1-setplayer,EFFECT_ORICA)) {
					if(retval.size() < 0 || retval.size() < 3)
					   releasable+= 0x1f000000;
			    }
			    ///////kdiy///////
				if(summon_procedure_effect && summon_procedure_effect->is_flag(EFFECT_FLAG_SPSUM_PARAM) && summon_procedure_effect->o_range)
					new_zone = (new_zone >> 16) | (new_zone & 0xffffu << 16);
				new_zone &= zone;
				if(summon_procedure_effect) {
					if(new_min_tribute < (int32_t)min_tribute)
						new_min_tribute = min_tribute;
					if(!target->is_summonable(summon_procedure_effect, new_min_tribute, new_zone, releasable, peff))
						continue;
				} else {
					int32_t rcount = target->get_set_tribute_count();
					uint16_t min = rcount & 0xffff;
					uint16_t max = (rcount >> 16) & 0xffff;
					if(!is_player_can_mset(SUMMON_TYPE_ADVANCE, setplayer, target, setplayer))
						max = 0;
					if(min < min_tribute)
						min = min_tribute;
					if(max < min)
						continue;
					if(min < new_min_tribute)
						min = new_min_tribute;
					if(!check_tribute(target, min, max, nullptr, target->current.controler, new_zone, releasable, POS_FACEDOWN_DEFENSE))
						continue;
				}
				core.select_effects.push_back(peff);
				core.select_options.push_back(peff->description);
			}
		}
		if(core.select_options.size() == 1)
			returns.set<int32_t>(0, 0);
		else
			emplace_process<Processors::SelectOption>(setplayer);
		return FALSE;
	}
	case 2: {
		effect* pextra = core.select_effects[returns.at<int32_t>(0)];
		arg.extra_summon_effect = pextra;
		uint32_t releasable = 0xff00ffu;
		if(pextra) {
			std::vector<lua_Integer> retval;
			pextra->get_value(target, 0, retval);
			uint8_t new_min_tribute = retval.size() > 0 ? static_cast<uint8_t>(retval[0]) : 0;
			uint32_t new_zone = retval.size() > 1 ? static_cast<uint32_t>(retval[1]) : 0x1f001f;
			///////kdiy///////
			if(is_player_affected_by_effect(setplayer,EFFECT_ORICA) && retval.size()<2)
				new_zone+= 0x1f00;
			if(is_player_affected_by_effect(1-setplayer,EFFECT_ORICA) && retval.size()<2)
				new_zone+= 0x1f0000;				  
			///////kdiy///////
			if(retval.size() > 2) {
				if(retval[2] < 0)
					releasable += static_cast<int32_t>(retval[2]);
				else
					releasable = static_cast<uint32_t>(retval[2]);
			}
			///////kdiy///////
			if(is_player_affected_by_effect(setplayer,EFFECT_ORICA)) {
				if(retval.size() < 0 || retval.size() < 3)
					releasable+= 0x1f00;  
				}
				if(is_player_affected_by_effect(1-setplayer,EFFECT_ORICA)) {
					if(retval.size() < 0 || retval.size() < 3)
					   releasable+= 0x1f000000; 
			    }
			///////kdiy///////
			if(min_tribute < new_min_tribute)
				arg.min_tribute = new_min_tribute;
			if(summon_procedure_effect && summon_procedure_effect->is_flag(EFFECT_FLAG_SPSUM_PARAM) && summon_procedure_effect->o_range)
				new_zone = (new_zone >> 16) | (new_zone & 0xffffu << 16);
			arg.zone &= new_zone;
		}
		if(summon_procedure_effect) {
			arg.step = 3;
			return FALSE;
		}
		core.select_cards.clear();
		int32_t required = target->get_set_tribute_count();
		int32_t min = required & 0xffff;
		int32_t max = required >> 16;
		if(min < min_tribute) {
			min = min_tribute;
		}
		uint32_t adv = is_player_can_mset(SUMMON_TYPE_ADVANCE, setplayer, target, setplayer);
		if(max == 0 || !adv) {
			return_cards.clear();
			arg.step = 3;
		} else {
			core.release_cards.clear();
			core.release_cards_ex.clear();
			core.release_cards_ex_oneof.clear();
			int32_t rcount = get_summon_release_list(target, &core.release_cards, &core.release_cards_ex, &core.release_cards_ex_oneof, nullptr, 0, releasable, POS_FACEDOWN_DEFENSE);
			if(rcount == 0) {
				return_cards.clear();
				arg.step = 3;
			} else {
				int32_t ct = get_tofield_count(target, setplayer, LOCATION_MZONE, setplayer, LOCATION_REASON_TOFIELD, zone);
				int32_t fcount = get_mzone_limit(setplayer, setplayer, LOCATION_REASON_TOFIELD);
				////////kdiy/////
			    if(is_player_affected_by_effect(setplayer,EFFECT_ORICA))
				  fcount+= get_szone_limit(setplayer, setplayer, LOCATION_REASON_TOFIELD);
				////////kdiy/////				
				if(min == 0 && ct > 0 && fcount > 0) {
					emplace_process<Processors::SelectYesNo>(setplayer, 90);
					arg.max_allowed_tributes = max;
				} else {
					if(min < -fcount + 1) {
						min = -fcount + 1;
					}
					select_tribute_cards(target, setplayer, core.summon_cancelable, min, max, setplayer, zone);
					arg.step = 3;
				}
			}
		}
		return FALSE;
	}
	case 3: {
		if(returns.at<int32_t>(0))
			return_cards.clear();
		else {
			select_tribute_cards(target, setplayer, core.summon_cancelable, 1, arg.max_allowed_tributes, setplayer, zone);
		}
		return FALSE;
	}
	case 4: {
		if(summon_procedure_effect)
			arg.step = 5;
		else {
			if(return_cards.canceled) {
				return TRUE;
			}
			if(!return_cards.list.empty()) {
				arg.tributes.clear();
				arg.tributes.insert(return_cards.list.begin(), return_cards.list.end());
			}
		}
		effect_set eset;
		target->filter_effect(EFFECT_MSET_COST, &eset);
		for(const auto& peff : eset) {
			if(peff->operation) {
				core.sub_solving_event.push_back(nil_event);
				emplace_process<Processors::ExecuteOperation>(peff, setplayer);
			}
		}
		return FALSE;
	}
	case 5: {
		auto& tributes = arg.tributes;
		target->summon.location = LOCATION_HAND;
		target->summon.pzone = false;
		target->summon.type = SUMMON_TYPE_NORMAL;
		if(!tributes.empty()) {
			for(auto& pcard : tributes)
				pcard->current.reason_card = target;
			target->set_material(tributes);
			release(std::move(tributes), nullptr, REASON_SUMMON | REASON_MATERIAL, setplayer);
			target->summon.type |= SUMMON_TYPE_ADVANCE;
			adjust_all();
		}
		target->summon.player = setplayer;
		target->current.reason_effect = nullptr;
		target->current.reason_player = setplayer;
		arg.step = 7;
		return FALSE;
	}
	case 6: {
		returns.set<int32_t>(0, TRUE);
		if(summon_procedure_effect->target) {
			auto pextra = arg.extra_summon_effect;
			uint32_t releasable = 0xff00ffu;
			if(pextra) {
				std::vector<lua_Integer> retval;
				pextra->get_value(target, 0, retval);
				if(retval.size() > 2) {
					if(retval[2] < 0)
						releasable += static_cast<int32_t>(retval[2]);
					else
						releasable = static_cast<uint32_t>(retval[2]);
				}
			}
			pduel->lua->add_param<LuaParam::CARD>(target);
			pduel->lua->add_param<LuaParam::INT>(min_tribute);
			pduel->lua->add_param<LuaParam::INT>(zone);
			pduel->lua->add_param<LuaParam::INT>(releasable);
			pduel->lua->add_param<LuaParam::EFFECT>(pextra);
			core.sub_solving_event.push_back(nil_event);
			emplace_process<Processors::ExecuteTarget>(summon_procedure_effect, setplayer);
		}
		return FALSE;
	}
	case 7: {
		if(!returns.at<int32_t>(0)) {
			return TRUE;
		}
		target->summon.type = (summon_procedure_effect->get_value(target) & 0xfffffff) | SUMMON_TYPE_NORMAL;
		target->summon.location = LOCATION_HAND;
		target->summon.pzone = false;
		target->summon.player = setplayer;
		target->current.reason_effect = summon_procedure_effect;
		target->current.reason_player = setplayer;
		if(summon_procedure_effect->operation) {
			auto pextra = arg.extra_summon_effect;
			uint32_t releasable = 0xff00ffu;
			if(pextra) {
				std::vector<lua_Integer> retval;
				pextra->get_value(target, 0, retval);
				if(retval.size() > 2) {
					if(retval[2] < 0)
						releasable += static_cast<int32_t>(retval[2]);
					else
						releasable = static_cast<uint32_t>(retval[2]);
				}
				///////kdiy///////
				if(is_player_affected_by_effect(setplayer,EFFECT_ORICA)) {
					if(retval.size() < 0 || retval.size() < 3)
					   releasable+= 0x1f00;  
				}
				if(is_player_affected_by_effect(1-setplayer,EFFECT_ORICA)) {
					if(retval.size() < 0 || retval.size() < 3)
					   releasable+= 0x1f000000; 
			    }
			    ///////kdiy///////
			}
			pduel->lua->add_param<LuaParam::CARD>(target);
			pduel->lua->add_param<LuaParam::INT>(min_tribute);
			pduel->lua->add_param<LuaParam::INT>(zone);
			pduel->lua->add_param<LuaParam::INT>(releasable);
			pduel->lua->add_param<LuaParam::EFFECT>(pextra);
			core.sub_solving_event.push_back(nil_event);
			emplace_process<Processors::ExecuteOperation>(summon_procedure_effect, setplayer);
		}
		summon_procedure_effect->dec_count(setplayer);
		return FALSE;
	}
	case 8: {
		break_effect();
		if(ignore_count)
			return FALSE;
		auto pextra = arg.extra_summon_effect;
		if(!pextra)
			++core.summon_count[setplayer];
		else {
			core.extra_summon[setplayer] = TRUE;
			auto message = pduel->new_message(MSG_HINT);
			message->write<uint8_t>(HINT_CARD);
			message->write<uint8_t>(0);
			message->write<uint64_t>(pextra->handler->data.code);
			if(pextra->operation) {
				pduel->lua->add_param<LuaParam::CARD>(target);
				core.sub_solving_event.push_back(nil_event);
				emplace_process<Processors::ExecuteOperation>(pextra, setplayer);
			}
		}
		return FALSE;
	}
	case 9: {
		uint8_t targetplayer = setplayer;
		uint8_t positions = POS_FACEDOWN_DEFENSE;
		if(summon_procedure_effect && summon_procedure_effect->is_flag(EFFECT_FLAG_SPSUM_PARAM)) {
			positions = (uint8_t)summon_procedure_effect->s_range & POS_FACEDOWN;
			if(summon_procedure_effect->o_range)
				targetplayer = 1 - setplayer;
		}
		target->enable_field_effect(false);	
	    ///////kdiy///////
		target->prev_temp.location = target->current.location;
		if(target->current.location == LOCATION_SZONE && target->is_affected_by_effect(EFFECT_ORICA_SZONE))
		    target->prev_temp.location = LOCATION_MZONE;
		if(target->current.location == LOCATION_MZONE && target->is_affected_by_effect(EFFECT_SANCT_MZONE))
		    target->prev_temp.location = LOCATION_SZONE;
		effect* oeffect = is_player_affected_by_effect(targetplayer,EFFECT_ORICA);
	    if(oeffect && !target->is_affected_by_effect(EFFECT_ORICA_SZONE)) {
		    effect* deffect = pduel->new_effect();
			deffect->owner = oeffect->owner;
			deffect->code = EFFECT_ORICA_SZONE;
			deffect->type = EFFECT_TYPE_SINGLE;
			deffect->flag[0] = EFFECT_FLAG_CANNOT_DISABLE | EFFECT_FLAG_IGNORE_IMMUNE | EFFECT_FLAG_UNCOPYABLE | EFFECT_FLAG_OWNER_RELATE;
			deffect->reset_flag = RESET_EVENT+0x1fe0000+RESET_CONTROL-RESET_TURN_SET;
			target->add_effect(deffect);
            target->reset(EFFECT_SANCT_MZONE, RESET_CODE);
		}
		///////kdiy///////
		move_to_field(target, setplayer, targetplayer, LOCATION_MZONE, positions, FALSE, 0, zone);		
		return FALSE;
	}
	case 10: {
		set_control(target, target->current.controler, 0, 0);
		core.phase_action = true;
		++core.normalsummon_state_count[setplayer];
		check_card_counter(target, ACTIVITY_NORMALSUMMON, setplayer);
		target->set_status(STATUS_SUMMON_TURN, TRUE);
		auto message = pduel->new_message(MSG_SET);
		message->write<uint32_t>(target->data.code);
		message->write(target->get_info_location());
        ///kdiy///////
        message->write<uint8_t>(setplayer);
        message->write<uint8_t>(1);
        ///kdiy///////
		adjust_instant();
		raise_event(target, EVENT_MSET, summon_procedure_effect, 0, setplayer, setplayer, 0);
		process_instant_event();
		if(core.current_chain.size() == 0) {
			adjust_all();
			core.hint_timing[setplayer] |= TIMING_MSET;
			emplace_process<Processors::PointEvent>(false, false, false);
		}
		return TRUE;
	}
	}
	return TRUE;
}
bool field::process(Processors::SpellSet& arg) {
	auto setplayer = arg.setplayer;
	auto toplayer = arg.toplayer;
	auto target = arg.target;
	auto reason_effect = arg.reason_effect;
	switch(arg.step) {
	case 0: {
		if(!(target->data.type & TYPE_FIELD) && get_useable_count(target, toplayer, LOCATION_SZONE, setplayer, LOCATION_REASON_TOFIELD) <= 0)
			return TRUE;
		//////////kdiy///////
		//if(target->data.type & TYPE_MONSTER && !target->is_affected_by_effect(EFFECT_MONSTER_SSET))
		if((target->data.type & TYPE_MONSTER) && (!target->is_affected_by_effect(EFFECT_MONSTER_SSET) && !target->is_affected_by_effect(EFFECT_SANCT_MZONE)))
		//////////kdiy///////
			return TRUE;
		///////////kdiy//////////
		//if(target->current.location == LOCATION_SZONE)
		if((target->current.location == LOCATION_SZONE && target->is_affected_by_effect(EFFECT_ORICA_SZONE)) || (target->current.location == LOCATION_MZONE && !target->is_affected_by_effect(EFFECT_SANCT_MZONE)))
		///////////kdiy//////////
			return TRUE;
		if(!is_player_can_sset(setplayer, target))
			return TRUE;
		if(target->is_affected_by_effect(EFFECT_CANNOT_SSET))
			return TRUE;
		effect_set eset;
		target->filter_effect(EFFECT_SSET_COST, &eset);
		for(const auto& peff : eset) {
			if(peff->operation) {
				core.sub_solving_event.push_back(nil_event);
				emplace_process<Processors::ExecuteOperation>(peff, setplayer);
			}
		}
		return FALSE;
	}
	case 1: {
		target->enable_field_effect(false);	
	    ///////kdiy///////
		target->prev_temp.location = target->current.location;
		if(target->current.location == LOCATION_SZONE && target->is_affected_by_effect(EFFECT_ORICA_SZONE))
		    target->prev_temp.location = LOCATION_MZONE;
		if(target->current.location == LOCATION_MZONE && target->is_affected_by_effect(EFFECT_SANCT_MZONE))
		    target->prev_temp.location = LOCATION_SZONE;
		effect* seffect = is_player_affected_by_effect(toplayer,EFFECT_SANCT);
	    if(seffect && !target->is_affected_by_effect(EFFECT_SANCT_MZONE)) {
		    effect* deffect = pduel->new_effect();
			deffect->owner = seffect->owner;
			deffect->code = EFFECT_SANCT_MZONE;
			deffect->type = EFFECT_TYPE_SINGLE;
			deffect->flag[0] = EFFECT_FLAG_CANNOT_DISABLE | EFFECT_FLAG_IGNORE_IMMUNE | EFFECT_FLAG_UNCOPYABLE;
			deffect->reset_flag = RESET_EVENT+0x1fe0000+RESET_CONTROL-RESET_TURN_SET;
			target->add_effect(deffect);
            target->reset(EFFECT_ORICA_SZONE, RESET_CODE);
		}
		///////kdiy///////
		move_to_field(target, setplayer, toplayer, LOCATION_SZONE, POS_FACEDOWN, FALSE, 0, (target->data.type & TYPE_FIELD) ? 0x1 << 5 : 0xff);		
		return FALSE;
	}
	case 2: {
		core.phase_action = true;
		target->set_status(STATUS_SET_TURN, TRUE);
		if(target->data.type & TYPE_MONSTER) {
			effect* peffect = target->is_affected_by_effect(EFFECT_MONSTER_SSET);
			int32_t type_val = peffect->get_value();
			peffect = pduel->new_effect();
			peffect->owner = target;
			peffect->type = EFFECT_TYPE_SINGLE;
			peffect->code = EFFECT_CHANGE_TYPE;
			peffect->reset_flag = RESET_EVENT + 0x1fe0000;
			peffect->value = type_val;
			target->add_effect(peffect);
		}
		auto message = pduel->new_message(MSG_SET);
		message->write<uint32_t>(target->data.code);
		message->write(target->get_info_location());
        ///kdiy///////
        message->write<uint8_t>(setplayer);
        message->write<uint8_t>(2);
        ///kdiy///////
		adjust_instant();
		raise_event(target, EVENT_SSET, reason_effect, 0, setplayer, setplayer, 0);
		process_instant_event();
		if(core.current_chain.size() == 0) {
			adjust_all();
			core.hint_timing[setplayer] |= TIMING_SSET;
			emplace_process<Processors::PointEvent>(false, false, false);
		}
	}
	}
	return TRUE;
}
bool field::process(Processors::SpellSetGroup& arg) {
	auto setplayer = arg.setplayer;
	auto toplayer = arg.toplayer;
	auto ptarget = arg.ptarget;
	auto confirm = arg.confirm;
	auto reason_effect = arg.reason_effect;
	auto& set_cards = arg.set_cards;
	switch(arg.step) {
	case 0: {
		core.operated_set.clear();
		for(auto& target : ptarget->container) {
			if((!(target->data.type & TYPE_FIELD) && get_useable_count(target, toplayer, LOCATION_SZONE, setplayer, LOCATION_REASON_TOFIELD) <= 0)
				|| (target->data.type & TYPE_MONSTER && !target->is_affected_by_effect(EFFECT_MONSTER_SSET))
				|| (target->current.location == LOCATION_SZONE)
				|| (!is_player_can_sset(setplayer, target))
				|| (target->is_affected_by_effect(EFFECT_CANNOT_SSET))) {
				continue;
			}
			set_cards.insert(target);
		}
		if(set_cards.empty()) {
			returns.set<int32_t>(0, 0);
			return TRUE;
		}
		effect_set eset;
 		for(auto& pcard : set_cards) {
			eset.clear();
			pcard->filter_effect(EFFECT_SSET_COST, &eset);
			for(const auto& peff : eset) {
				if(peff->operation) {
					core.sub_solving_event.push_back(nil_event);
					emplace_process<Processors::ExecuteOperation>(peff, setplayer);
				}
			}
		}
		core.set_group_pre_set.clear();
		core.set_group_set.clear();
		core.set_group_used_zones = 0;
		core.phase_action = true;
		return FALSE;
	}
	case 1: {
		card* target = *set_cards.begin();
		if(target->data.type & TYPE_FIELD) {
			returns.set<int8_t>(2, 5);
			return FALSE;
		}
		uint32_t flag;
		get_useable_count(target, toplayer, LOCATION_SZONE, setplayer, LOCATION_REASON_TOFIELD, 0xff, &flag);
		flag |= core.set_group_used_zones;
		///kdiy///////
		// if(setplayer == toplayer) {
		// 	flag = ((flag & 0xff) << 8) | 0xffff00ff;
		// } else {
		// 	flag = ((flag & 0xff) << 24) | 0xffffff;
		// }
		//flag |= 0xe080e080;
		if(setplayer == toplayer) {
            if(is_player_affected_by_effect(toplayer, EFFECT_SANCT))
			    flag = ((flag & 0xff1f)) | 0xffffe0e0;
            else
			    flag = ((flag & 0xff00)) | 0xffffe0ff;
		} else {
			if(is_player_affected_by_effect(toplayer, EFFECT_SANCT))
			    flag = ((flag & 0xff1f) << 16) | 0xe0e0ffff;
            else
			    flag = ((flag & 0xff00) << 16) | 0xe0ffffff;
		}
		///kdiy///////
		auto message = pduel->new_message(MSG_HINT);
		message->write<uint8_t>(HINT_SELECTMSG);
		message->write<uint8_t>(setplayer);
		message->write<uint64_t>(target->data.code);
		emplace_process<Processors::SelectPlace>(setplayer, flag, 1);
		return FALSE;
	}
	case 2: {
		card* target = *set_cards.begin();
		uint32_t seq = returns.at<int8_t>(2);
		core.set_group_seq[core.set_group_pre_set.size()] = seq;
		core.set_group_pre_set.insert(target);
		core.set_group_used_zones |= (1 << seq);
		set_cards.erase(target);
		if(!set_cards.empty())
			arg.step = 0;
		return FALSE;
	}
	case 3: {
		card* target = *core.set_group_pre_set.begin();
		target->enable_field_effect(false);
		uint32_t zone{};
		if(target->data.type & TYPE_FIELD) {
			zone = 1 << 5;
		} else {
			for(uint32_t i = 0; i < 7; ++i) {
				zone = 1 << i;
				if(core.set_group_used_zones & zone) {
					core.set_group_used_zones &= ~zone;
					break;
				}
			}
		}	
		/////kdiy////////////
		target->prev_temp.location = target->current.location;
		if(target->current.location == LOCATION_SZONE && target->is_affected_by_effect(EFFECT_ORICA_SZONE))
		    target->prev_temp.location = LOCATION_MZONE;
		if(target->current.location == LOCATION_MZONE && target->is_affected_by_effect(EFFECT_SANCT_MZONE))
		    target->prev_temp.location = LOCATION_SZONE;
		/////kdiy////////////
		move_to_field(target, setplayer, toplayer, LOCATION_SZONE, POS_FACEDOWN, FALSE, 0, zone, FALSE, 0, FALSE);
		return FALSE;
	}
	case 4: {
		card* target = *core.set_group_pre_set.begin();
		target->set_status(STATUS_SET_TURN, TRUE);
		if(target->data.type & TYPE_MONSTER) {
			effect* peffect = target->is_affected_by_effect(EFFECT_MONSTER_SSET);
			int32_t type_val = peffect->get_value();
			peffect = pduel->new_effect();
			peffect->owner = target;
			peffect->type = EFFECT_TYPE_SINGLE;
			peffect->code = EFFECT_CHANGE_TYPE;
			peffect->reset_flag = RESET_EVENT + 0x1fe0000;
			peffect->value = type_val;
			target->add_effect(peffect);
		}
		auto message = pduel->new_message(MSG_SET);
		message->write<uint32_t>(target->data.code);
		message->write(target->get_info_location());
        ///kdiy///////
        message->write<uint8_t>(setplayer);
        message->write<uint8_t>(2);
        ///kdiy///////
		core.set_group_set.insert(target);
		core.set_group_pre_set.erase(target);
		if(!core.set_group_pre_set.empty())
			arg.step = 2;
		return FALSE;
	}
	case 5: {
		if(confirm) {
			auto message = pduel->new_message(MSG_CONFIRM_CARDS);
			message->write<uint8_t>(toplayer);
			message->write<uint32_t>(core.set_group_set.size());
			for(auto& pcard : core.set_group_set) {
				message->write<uint32_t>(pcard->data.code);
				message->write<uint8_t>(pcard->current.controler);
				message->write<uint8_t>(pcard->current.location);
				message->write<uint32_t>(pcard->current.sequence);
			}
		}
		return FALSE;
	}
	case 6: {
		core.operated_set.clear();
		for(auto& pcard : core.set_group_set) {
			core.operated_set.insert(pcard);
		}
		uint8_t ct = static_cast<uint8_t>(core.operated_set.size());
		if(core.set_group_used_zones & (1 << 5))
			--ct;
		if(ct <= 1)
			return FALSE;
		auto message = pduel->new_message(MSG_SHUFFLE_SET_CARD);
		message->write<uint8_t>(LOCATION_SZONE);
		message->write<uint8_t>(ct);
		uint8_t i = 0;
		for(const auto& pcard : core.operated_set) {
			uint8_t seq = core.set_group_seq[i++];
			if(pcard->data.type & TYPE_FIELD)
				continue;
			message->write(pcard->get_info_location());
			player[toplayer].list_szone[seq] = pcard;
			pcard->current.sequence = seq;
		}
		for(i = 0; i < ct; ++i) {
			message->write(loc_info{});
		}
		return FALSE;
	}
	case 7: {
		adjust_instant();
		raise_event(core.operated_set, EVENT_SSET, reason_effect, 0, setplayer, setplayer, 0);
		process_instant_event();
		if(core.current_chain.size() == 0) {
			adjust_all();
			core.hint_timing[setplayer] |= TIMING_SSET;
			emplace_process<Processors::PointEvent>(false, false, false);
		}
		return FALSE;
	}
	case 8: {
		returns.set<int32_t>(0, static_cast<int32_t>(core.operated_set.size()));
		return TRUE;
	}
	}
	return TRUE;
}
bool field::process(Processors::SpSummonRule& arg) {
	auto sumplayer = arg.sumplayer;
	auto target = arg.target;
	auto summon_type = arg.summon_type;
	switch(arg.step) {
	case 0: {
		effect_set eset;
		auto must_mats = core.must_use_mats;
		auto materials = core.only_use_mats;
		int32_t minc = core.forced_summon_minc;
		int32_t maxc = core.forced_summon_maxc;
		target->filter_spsummon_procedure(sumplayer, &eset, summon_type);
		target->filter_spsummon_procedure_g(sumplayer, &eset);
		core.must_use_mats = must_mats;
		core.only_use_mats = materials;
		core.forced_summon_minc = minc;
		core.forced_summon_maxc = maxc;
		if(!eset.size())
			return TRUE;
		core.select_effects.clear();
		core.select_options.clear();
		for(const auto& peff : eset) {
			core.select_effects.push_back(peff);
			core.select_options.push_back(peff->description);
		}
		if(core.select_options.size() == 1)
			returns.set<int32_t>(0, 0);
		else
			emplace_process<Processors::SelectOption>(sumplayer);
		return FALSE;
	}
	case 1: {
		effect* peffect = core.select_effects[returns.at<int32_t>(0)];
		arg.summon_proc_effect = peffect;
		if(peffect->code == EFFECT_SPSUMMON_PROC_G) {
			arg.step = 19;
			return FALSE;
		}
		returns.set<int32_t>(0, TRUE);
		if(peffect->target) {
			pduel->lua->add_param<LuaParam::CARD>(target);
			pduel->lua->add_param<LuaParam::GROUP>(core.must_use_mats);
			pduel->lua->add_param<LuaParam::GROUP>(core.only_use_mats);
			if(core.forced_summon_minc) {
				pduel->lua->add_param<LuaParam::INT>(core.forced_summon_minc);
				pduel->lua->add_param<LuaParam::INT>(core.forced_summon_maxc);
			}
			core.sub_solving_event.push_back(nil_event);
			emplace_process<Processors::ExecuteTarget>(peffect, sumplayer);
		}
		return FALSE;
	}
	case 2: {
		if(!returns.at<int32_t>(0))
			return TRUE;
		arg.spsummon_cost_effects.clear();
		target->filter_effect(EFFECT_SPSUMMON_COST, &arg.spsummon_cost_effects);
		for(const auto& peffect : arg.spsummon_cost_effects) {
			if(peffect->operation) {
				core.sub_solving_event.push_back(nil_event);
				emplace_process<Processors::ExecuteOperation>(peffect, sumplayer);
			}
		}
		return FALSE;
	}
	case 3: {
		auto proc = arg.summon_proc_effect;
		target->material_cards.clear();
		if(proc->operation) {
			pduel->lua->add_param<LuaParam::CARD>(target);
			pduel->lua->add_param<LuaParam::GROUP>(core.must_use_mats);
			pduel->lua->add_param<LuaParam::GROUP>(core.only_use_mats);
			if(core.forced_summon_minc) {
				pduel->lua->add_param<LuaParam::INT>(core.forced_summon_minc);
				pduel->lua->add_param<LuaParam::INT>(core.forced_summon_maxc);
			}
			core.forced_summon_minc = 0;
			core.forced_summon_maxc = 0;
			core.sub_solving_event.push_back(nil_event);
			emplace_process<Processors::ExecuteOperation>(proc, sumplayer);
		}
		proc->dec_count(sumplayer);
		return FALSE;
	}
	case 4: {
		core.must_use_mats = nullptr;
		core.only_use_mats = nullptr;
		auto proc = arg.summon_proc_effect;
		uint8_t targetplayer = sumplayer;
		uint8_t positions = POS_FACEUP;
		if(proc->is_flag(EFFECT_FLAG_SPSUM_PARAM)) {
			positions = (uint8_t)proc->s_range;
			if(proc->o_range == 0)
				targetplayer = sumplayer;
			else
				targetplayer = 1 - sumplayer;
		}
		if(positions == 0)
			positions = POS_FACEUP_ATTACK;
		std::vector<lua_Integer> retval;
		proc->get_value(target, 0, retval);
		const auto sumtype = (retval.size() > 0 ? (static_cast<uint32_t>(retval[0]) & 0xf00ffff) : 0) | SUMMON_TYPE_SPECIAL;
		uint32_t zone = retval.size() > 1 ? static_cast<uint32_t>(retval[1]) : 0xff;
		/////kdiy/////////
		if(is_player_affected_by_effect(targetplayer, EFFECT_ORICA))
		    zone = retval.size() > 1 ? retval[1] : 0x1fff;
		/////kdiy/////////
		target->summon.type = sumtype;
		target->summon.location = target->current.location;
		target->summon.sequence = target->current.sequence;
		target->summon.pzone = target->current.pzone;
		target->enable_field_effect(false);
		effect_set eset;
		filter_player_effect(sumplayer, EFFECT_FORCE_SPSUMMON_POSITION, &eset);
		for(auto& eff : eset) {
			if(eff->target) {
				pduel->lua->add_param<LuaParam::EFFECT>(eff);
				pduel->lua->add_param<LuaParam::CARD>(target);
				pduel->lua->add_param<LuaParam::INT>(sumplayer);
				pduel->lua->add_param<LuaParam::INT>(sumtype);
				pduel->lua->add_param<LuaParam::INT>(positions);
				pduel->lua->add_param<LuaParam::INT>(targetplayer);
				pduel->lua->add_param<LuaParam::EFFECT>(proc);
				if(!pduel->lua->check_condition(eff->target, 7))
					continue;
			}
			positions &= eff->get_value();
		}	
	    ///////kdiy///////
		target->prev_temp.location = target->current.location;
		if(target->current.location == LOCATION_SZONE && target->is_affected_by_effect(EFFECT_ORICA_SZONE))
		    target->prev_temp.location = LOCATION_MZONE;
		if(target->current.location == LOCATION_MZONE && target->is_affected_by_effect(EFFECT_SANCT_MZONE))
		    target->prev_temp.location = LOCATION_SZONE;
		effect* oeffect = is_player_affected_by_effect(targetplayer,EFFECT_ORICA);
	    if(oeffect && !target->is_affected_by_effect(EFFECT_ORICA_SZONE)) {
		    effect* deffect = pduel->new_effect();
			deffect->owner = oeffect->owner;
			deffect->code = EFFECT_ORICA_SZONE;
			deffect->type = EFFECT_TYPE_SINGLE;
			deffect->flag[0] = EFFECT_FLAG_CANNOT_DISABLE | EFFECT_FLAG_IGNORE_IMMUNE | EFFECT_FLAG_UNCOPYABLE | EFFECT_FLAG_OWNER_RELATE;
			deffect->reset_flag = RESET_EVENT+0x1fe0000+RESET_CONTROL-RESET_TURN_SET;
			target->add_effect(deffect);
            target->reset(EFFECT_SANCT_MZONE, RESET_CODE);
		}
		///////kdiy///////
		move_to_field(target, sumplayer, targetplayer, LOCATION_MZONE, positions, FALSE, 0, zone, TRUE);			
		target->current.reason = REASON_SPSUMMON;
		target->current.reason_effect = proc;
		target->current.reason_player = sumplayer;
		target->summon.player = sumplayer;
		if(is_flag(DUEL_CANNOT_SUMMON_OATH_OLD)) {
			set_spsummon_counter(sumplayer);
			check_card_counter(target, ACTIVITY_SPSUMMON, sumplayer);
		}
		if(is_flag(DUEL_SPSUMMON_ONCE_OLD_NEGATE) && target->spsummon_code)
			++core.spsummon_once_map[sumplayer][target->spsummon_code];
		break_effect();
		return FALSE;
	}
	case 5: {
		if(core.shuffle_hand_check[0])
			shuffle(0, LOCATION_HAND);
		if(core.shuffle_hand_check[1])
			shuffle(1, LOCATION_HAND);
		if(core.shuffle_deck_check[0])
			shuffle(0, LOCATION_DECK);
		if(core.shuffle_deck_check[1])
			shuffle(1, LOCATION_DECK);
		set_control(target, target->current.controler, 0, 0);
		core.phase_action = true;
		target->current.reason_effect = arg.summon_proc_effect;
		auto message = pduel->new_message(MSG_SPSUMMONING);
		message->write<uint32_t>(target->data.code);
		message->write(target->get_info_location());
        /////kdiy///////
        message->write<uint32_t>(target->summon.type);
        message->write<uint8_t>(sumplayer);
		message->write<bool>(target->single_effect.count((1295111 & 0xfffffff) | 0x10000000) > 0);
        /////kdiy///////
		return FALSE;
	}
	case 6: {
		auto proc = arg.summon_proc_effect;
		int32_t matreason = REASON_SPSUMMON;
		if(proc->value == SUMMON_TYPE_SYNCHRO)
			matreason = REASON_SYNCHRO;
		else if(proc->value == SUMMON_TYPE_XYZ)
			matreason = REASON_XYZ;
		else if(proc->value == SUMMON_TYPE_LINK)
			matreason = REASON_LINK;
		if (target->material_cards.size()) {
			for (auto& mcard : target->material_cards)
				raise_single_event(mcard, nullptr, EVENT_BE_PRE_MATERIAL, proc, matreason, sumplayer, sumplayer, 0);
		}
		raise_event(target->material_cards, EVENT_BE_PRE_MATERIAL, proc, matreason, sumplayer, sumplayer, 0);
		process_single_event();
		process_instant_event();
		return FALSE;
	}
	case 7: {
		if(core.current_chain.size() == 0) {
			if(target->is_affected_by_effect(EFFECT_CANNOT_DISABLE_SPSUMMON))
				arg.step = 14;
			else
				arg.step = 9;
			return FALSE;
		} else {
			arg.step = 14;
			return FALSE;
		}
		return FALSE;
	}
	case 10: {
		target->set_status(STATUS_SUMMONING, TRUE);
		target->set_status(STATUS_SUMMON_DISABLED, FALSE);
		raise_event(target, EVENT_SPSUMMON, arg.summon_proc_effect, 0, sumplayer, sumplayer, 0);
		process_instant_event();
		emplace_process<Processors::PointEvent>(true, true, true);
		return FALSE;
	}
	case 11: {
		if(target->is_status(STATUS_SUMMONING)) {
			arg.step = 14;
			return FALSE;
		}
		for(const auto& peffect : arg.spsummon_cost_effects)
			remove_oath_effect(peffect);
		if(!is_flag(DUEL_CANNOT_SUMMON_OATH_OLD)) {
			auto proc = arg.summon_proc_effect;
			remove_oath_effect(proc);
			if(proc->is_flag(EFFECT_FLAG_COUNT_LIMIT) && (proc->count_flag & EFFECT_COUNT_CODE_OATH)) {
				dec_effect_code(proc->count_code, proc->count_flag, proc->count_hopt_index, sumplayer);
			}
		}
		///////////kdiy//////////
		//if(target->current.location == LOCATION_MZONE)
		if((target->current.location == LOCATION_MZONE && !target->is_affected_by_effect(EFFECT_SANCT_MZONE)) || (target->current.location == LOCATION_SZONE && target->is_affected_by_effect(EFFECT_ORICA_SZONE)))
		///////////kdiy//////////
			send_to(target, nullptr, REASON_RULE, sumplayer, sumplayer, LOCATION_GRAVE, 0, 0);
		adjust_instant();
		emplace_process<Processors::PointEvent>(false, false, false);
		return TRUE;
	}
	case 15: {
		release_oath_relation(arg.summon_proc_effect);
		for(const auto& peffect : arg.spsummon_cost_effects)
			release_oath_relation(peffect);
		target->set_status(STATUS_SUMMONING, FALSE);
		target->set_status(STATUS_PROC_COMPLETE | STATUS_SPSUMMON_TURN, TRUE);
		target->enable_field_effect(true);
		if(target->is_status(STATUS_DISABLED))
			target->reset(RESET_DISABLE, RESET_EVENT);
		return FALSE;
	}
	case 16: {
		pduel->new_message(MSG_SPSUMMONED);
		adjust_instant();
		auto proc = arg.summon_proc_effect;
		int32_t matreason = REASON_SPSUMMON;
		if(proc->value == SUMMON_TYPE_SYNCHRO)
			matreason = REASON_SYNCHRO;
		else if(proc->value == SUMMON_TYPE_XYZ)
			matreason = REASON_XYZ;
		else if(proc->value == SUMMON_TYPE_LINK)
			matreason = REASON_LINK;
		if(target->material_cards.size()) {
			for(auto& mcard : target->material_cards)
				raise_single_event(mcard, nullptr, EVENT_BE_MATERIAL, proc, matreason, sumplayer, sumplayer, 0);
		}
		raise_event(target->material_cards, EVENT_BE_MATERIAL, proc, matreason, sumplayer, sumplayer, 0);
		process_single_event();
		process_instant_event();
		return FALSE;
	}
	case 17: {
		if(!is_flag(DUEL_CANNOT_SUMMON_OATH_OLD)) {
			set_spsummon_counter(sumplayer);
			check_card_counter(target, ACTIVITY_SPSUMMON, sumplayer);
		}
		if(!is_flag(DUEL_SPSUMMON_ONCE_OLD_NEGATE) && target->spsummon_code)
			++core.spsummon_once_map[sumplayer][target->spsummon_code];
		raise_single_event(target, nullptr, EVENT_SPSUMMON_SUCCESS, arg.summon_proc_effect, 0, sumplayer, sumplayer, 0);
		process_single_event();
		////kdiy////////
		raise_event(target, EVENT_PRESPSUMMON_SUCCESS, arg.summon_proc_effect, 0, sumplayer, sumplayer, 0);
		process_instant_event();
		////kdiy////////
		raise_event(target, EVENT_SPSUMMON_SUCCESS, arg.summon_proc_effect, 0, sumplayer, sumplayer, 0);
		process_instant_event();
		if(core.current_chain.size() == 0) {
			adjust_all();
			core.hint_timing[sumplayer] |= TIMING_SPSUMMON;
			emplace_process<Processors::PointEvent>(false, false, false);
		}
		return TRUE;
	}
	case 20: {
		auto proc = arg.summon_proc_effect;
		arg.cards_to_summon_g = pduel->new_group();
		if(proc->operation) {
			core.sub_solving_event.push_back(nil_event);
			pduel->lua->add_param<LuaParam::CARD>(target);
			pduel->lua->add_param<LuaParam::GROUP>(arg.cards_to_summon_g);
			pduel->lua->add_param<LuaParam::BOOLEAN>(arg.is_mid_chain);
			emplace_process<Processors::ExecuteOperation>(proc, sumplayer);
		}
		proc->dec_count(sumplayer);
		return FALSE;
	}
	case 21: {
		auto pgroup = arg.cards_to_summon_g;
		for(auto cit = pgroup->container.begin(); cit != pgroup->container.end(); ) {
			card* pcard = *cit++;
			if(!(pcard->data.type & TYPE_MONSTER)
				///////////kdiy//////////				
				//|| (pcard->current.location == LOCATION_MZONE)
				|| (pcard->current.location == LOCATION_MZONE && !pcard->is_affected_by_effect(EFFECT_SANCT_MZONE)) 
				|| (pcard->current.location == LOCATION_SZONE && pcard->is_affected_by_effect(EFFECT_ORICA_SZONE))
				///////////kdiy//////////
				|| check_unique_onfield(pcard, sumplayer, LOCATION_MZONE)
				|| pcard->is_affected_by_effect(EFFECT_CANNOT_SPECIAL_SUMMON)) {
				pgroup->container.erase(pcard);
				continue;
			}
			arg.spsummon_cost_effects.clear();
			pcard->filter_effect(EFFECT_SPSUMMON_COST, &arg.spsummon_cost_effects);
			for(const auto& peffect : arg.spsummon_cost_effects) {
				if(peffect->operation) {
					core.sub_solving_event.push_back(nil_event);
					emplace_process<Processors::ExecuteOperation>(peffect, sumplayer);
				}
			}
		}
		return FALSE;
	}
	case 22: {
		auto pgroup = arg.cards_to_summon_g;
		if(pgroup->container.size() == 0)
			return TRUE;
		core.phase_action = true;
		pgroup->it = pgroup->container.begin();
		return FALSE;
	}
	case 23: {
		auto proc = arg.summon_proc_effect;
		auto pgroup = arg.cards_to_summon_g;
		card* pcard = *pgroup->it;
		pcard->enable_field_effect(false);
		pcard->current.reason = REASON_SPSUMMON;
		pcard->current.reason_effect = proc;
		pcard->current.reason_player = sumplayer;
		pcard->summon.player = sumplayer;
		const auto sumtype = (proc->get_value(pcard) & 0xff00ffff) | SUMMON_TYPE_SPECIAL;
		pcard->summon.type = sumtype;
		pcard->summon.location = pcard->current.location;
		pcard->summon.sequence = pcard->current.sequence;
		pcard->summon.pzone = pcard->current.pzone;
		effect_set eset;
		uint8_t positions = POS_FACEUP;
		filter_player_effect(sumplayer, EFFECT_FORCE_SPSUMMON_POSITION, &eset);
		for(auto& eff : eset) {
			if(eff->target) {
				pduel->lua->add_param<LuaParam::EFFECT>(eff);
				pduel->lua->add_param<LuaParam::CARD>(pcard);
				pduel->lua->add_param<LuaParam::INT>(sumplayer);
				pduel->lua->add_param<LuaParam::INT>(sumtype);
				pduel->lua->add_param<LuaParam::INT>(positions);
				pduel->lua->add_param<LuaParam::INT>(sumplayer);
				pduel->lua->add_param<LuaParam::EFFECT>(proc);
				if(!pduel->lua->check_condition(eff->target, 7))
					continue;
			}
			positions &= eff->get_value();
		}
		if(is_flag(DUEL_CANNOT_SUMMON_OATH_OLD)) {
			check_card_counter(pcard, ACTIVITY_SPSUMMON, sumplayer);
		}
		uint32_t zone = 0xff;
		uint32_t flag1, flag2;
		int32_t ct1 = get_tofield_count(pcard, sumplayer, LOCATION_MZONE, sumplayer, LOCATION_REASON_TOFIELD, zone, &flag1);
		int32_t ct2 = get_spsummonable_count_fromex(pcard, sumplayer, sumplayer, zone, &flag2);
		for(auto it = pgroup->it; it != pgroup->container.end(); ++it) {
			if((*it)->current.location != LOCATION_EXTRA)
				--ct1;
			else
				--ct2;
		}
		if(pcard->current.location != LOCATION_EXTRA) {
			if(ct2 == 0)
				zone = flag2;
		} else {
			if(ct1 == 0)
				zone = flag1;
		}
	    ///////kdiy///////
		pcard->prev_temp.location = pcard->current.location;
		if(pcard->current.location == LOCATION_SZONE && pcard->is_affected_by_effect(EFFECT_ORICA_SZONE))
		    pcard->prev_temp.location = LOCATION_MZONE;
		if(pcard->current.location == LOCATION_MZONE && pcard->is_affected_by_effect(EFFECT_SANCT_MZONE))
		    pcard->prev_temp.location = LOCATION_SZONE;
		effect* oeffect = is_player_affected_by_effect(sumplayer,EFFECT_ORICA);
	    if(oeffect && !pcard->is_affected_by_effect(EFFECT_ORICA_SZONE)) {
		    effect* deffect = pduel->new_effect();
			deffect->owner = oeffect->owner;
			deffect->code = EFFECT_ORICA_SZONE;
			deffect->type = EFFECT_TYPE_SINGLE;
			deffect->flag[0] = EFFECT_FLAG_CANNOT_DISABLE | EFFECT_FLAG_IGNORE_IMMUNE | EFFECT_FLAG_UNCOPYABLE | EFFECT_FLAG_OWNER_RELATE;
			deffect->reset_flag = RESET_EVENT+0x1fe0000+RESET_CONTROL-RESET_TURN_SET;
			pcard->add_effect(deffect);
            pcard->reset(EFFECT_SANCT_MZONE, RESET_CODE);
		}
		///////kdiy///////
		move_to_field(pcard, sumplayer, sumplayer, LOCATION_MZONE, positions, FALSE, 0, zone, TRUE);							
		return FALSE;
	}
	case 24: {
		auto pgroup = arg.cards_to_summon_g;
		card* pcard = *pgroup->it++;
		auto message = pduel->new_message(MSG_SPSUMMONING);
		message->write<uint32_t>(pcard->data.code);
		message->write(pcard->get_info_location());
		//kdiy///////
        message->write<uint32_t>(pcard->summon.type);
        message->write<uint8_t>(sumplayer);
		message->write<bool>(false);
		//kdiy///////
		set_control(pcard, pcard->current.controler, 0, 0);
		pcard->set_status(STATUS_SPSUMMON_STEP, TRUE);
		if(pgroup->it != pgroup->container.end())
			arg.step = 22;
		return FALSE;
	}
	case 25: {
		auto pgroup = arg.cards_to_summon_g;
		if(is_flag(DUEL_CANNOT_SUMMON_OATH_OLD)) {
			set_spsummon_counter(sumplayer);
		}
		if(is_flag(DUEL_SPSUMMON_ONCE_OLD_NEGATE)) {
			std::set<uint32_t> spsummon_once_set;
			for(auto& pcard : pgroup->container) {
				if(pcard->spsummon_code)
					spsummon_once_set.insert(pcard->spsummon_code);
			}
			for(auto& cit : spsummon_once_set)
				++core.spsummon_once_map[sumplayer][cit];
		}
		if(core.current_chain.size() == 0) {
			card_set cset;
			for(auto& pcard : pgroup->container) {
				pcard->set_status(STATUS_SUMMONING, TRUE);
				pcard->set_status(STATUS_SPSUMMON_STEP, FALSE);
				if(!pcard->is_affected_by_effect(EFFECT_CANNOT_DISABLE_SPSUMMON)) {
					cset.insert(pcard);
				}
			}
			if(cset.size()) {
				raise_event(std::move(cset), EVENT_SPSUMMON, arg.summon_proc_effect, 0, sumplayer, sumplayer, 0);
				process_instant_event();
				emplace_process<Processors::PointEvent>(true, true, true);
			}
		} else {
			for(auto& pcard : pgroup->container)
				pcard->set_status(STATUS_SPSUMMON_STEP, FALSE);
			arg.step = 26;
		}
		return FALSE;
	}
	case 26: {
		auto pgroup = arg.cards_to_summon_g;
		card_set cset;
		for(auto cit = pgroup->container.begin(); cit != pgroup->container.end(); ) {
			card* pcard = *cit++;
			if(!pcard->is_status(STATUS_SUMMONING)) {
				pgroup->container.erase(pcard);
				///////////kdiy//////////			
				//if(pcard->current.location == LOCATION_MZONE)
				if((pcard->current.location == LOCATION_MZONE && !pcard->is_affected_by_effect(EFFECT_SANCT_MZONE)) || (pcard->current.location == LOCATION_SZONE && pcard->is_affected_by_effect(EFFECT_ORICA_SZONE)))
				///////////kdiy//////////	
					cset.insert(pcard);
			}
		}
		if(cset.size()) {
			send_to(std::move(cset), nullptr, REASON_RULE, sumplayer, sumplayer, LOCATION_GRAVE, 0, 0);
			adjust_instant();
		}
		if(pgroup->container.size() == 0) {
			if(!is_flag(DUEL_CANNOT_SUMMON_OATH_OLD)) {
				auto proc = arg.summon_proc_effect;
				remove_oath_effect(proc);
				if(proc->is_flag(EFFECT_FLAG_COUNT_LIMIT) && (proc->count_flag & EFFECT_COUNT_CODE_OATH)) {
					dec_effect_code(proc->count_code, proc->count_flag, proc->count_hopt_index, sumplayer);
				}
			}
			for(const auto& peffect : arg.spsummon_cost_effects)
				remove_oath_effect(peffect);
			emplace_process<Processors::PointEvent>(false, false, false);
			return TRUE;
		}
		return FALSE;
	}
	case 27: {
		auto pgroup = arg.cards_to_summon_g;
		release_oath_relation(arg.summon_proc_effect);
		for(const auto& peffect : arg.spsummon_cost_effects)
			release_oath_relation(peffect);
		for(auto& pcard : pgroup->container) {
			pcard->set_status(STATUS_SUMMONING, FALSE);
			pcard->set_status(STATUS_SPSUMMON_TURN, TRUE);
			pcard->enable_field_effect(true);
			if(pcard->is_status(STATUS_DISABLED))
				pcard->reset(RESET_DISABLE, RESET_EVENT);
		}
		return FALSE;
	}
	case 28: {
		auto pgroup = arg.cards_to_summon_g;
		pduel->new_message(MSG_SPSUMMONED);
		if(!is_flag(DUEL_CANNOT_SUMMON_OATH_OLD)) {
			set_spsummon_counter(sumplayer);
			check_card_counter(pgroup, ACTIVITY_SPSUMMON, sumplayer);
		}
		if(!is_flag(DUEL_SPSUMMON_ONCE_OLD_NEGATE)) {
			std::set<uint32_t> spsummon_once_set;
			for(auto& pcard : pgroup->container) {
				if(pcard->spsummon_code)
					spsummon_once_set.insert(pcard->spsummon_code);
			}
			for(auto& cit : spsummon_once_set)
				++core.spsummon_once_map[sumplayer][cit];
		}
		for(auto& pcard : pgroup->container)
			raise_single_event(pcard, nullptr, EVENT_SPSUMMON_SUCCESS, pcard->current.reason_effect, 0, pcard->current.reason_player, pcard->summon.player, 0);
		process_single_event();
		////kdiy////////
		raise_event(pgroup->container, EVENT_PRESPSUMMON_SUCCESS, arg.summon_proc_effect, 0, sumplayer, sumplayer, 0);
		process_instant_event();
		////kdiy////////
		raise_event(pgroup->container, EVENT_SPSUMMON_SUCCESS, arg.summon_proc_effect, 0, sumplayer, sumplayer, 0);
		process_instant_event();
		if(core.current_chain.size() == 0) {
			adjust_all();
			core.hint_timing[sumplayer] |= TIMING_SPSUMMON;
			emplace_process<Processors::PointEvent>(false, false, false);
		}
		return TRUE;
	}
	}
	return TRUE;
}
bool field::process(Processors::SpSummonRuleGroup& arg) {
	auto sumplayer = arg.sumplayer;
	auto summon_type = arg.summon_type;
	switch(arg.step) {
	case 0: {
		effect_set eset;
		filter_field_effect(EFFECT_SPSUMMON_PROC_G, &eset);
		core.select_effects.clear();
		core.select_options.clear();
		for(const auto& peff : eset) {
			if((uint32_t)peff->get_value() != summon_type)
				continue;
			card* pcard = peff->get_handler();
			if(pcard->current.controler != sumplayer && !peff->is_flag(EFFECT_FLAG_BOTH_SIDE))
				continue;
			effect* oreason = core.reason_effect;
			uint8_t op = core.reason_player;
			core.reason_effect = peff;
			core.reason_player = pcard->current.controler;
			save_lp_cost();
			pduel->lua->add_param<LuaParam::EFFECT>(peff);
			pduel->lua->add_param<LuaParam::CARD>(pcard);
			pduel->lua->add_param<LuaParam::BOOLEAN>(TRUE);
			pduel->lua->add_param<LuaParam::EFFECT>(oreason);
			pduel->lua->add_param<LuaParam::INT>(sumplayer);
			if(pduel->lua->check_condition(peff->condition, 5)) {
				core.select_effects.push_back(peff);
				core.select_options.push_back(peff->description);
			}
			restore_lp_cost();
			core.reason_effect = oreason;
			core.reason_player = op;
		}
		if(core.select_options.empty())
			return TRUE;
		if(core.select_options.size() == 1)
			returns.set<int32_t>(0, 0);
		else
			emplace_process<Processors::SelectOption>(sumplayer);
		return FALSE;
	}
	case 1: {
		effect* peffect = core.select_effects[returns.at<int32_t>(0)];
		emplace_process<Processors::SpSummonRule>(Step{ 20 }, sumplayer, peffect->get_handler(), summon_type, true, peffect);
		return TRUE;
	}
	}
	return FALSE;
}
bool field::process(Processors::SpSummonStep& arg) {
	auto targets = arg.targets;
	auto target = arg.target;
	auto zone = arg.zone;
	uint8_t playerid = (target->spsummon_param >> 24) & 0xf;
	uint8_t nocheck = (target->spsummon_param >> 16) & 0xff;
	uint8_t nolimit = (target->spsummon_param >> 8) & 0xff;
	uint8_t positions = target->spsummon_param & 0xff;
	switch(arg.step) {
	case 0: {
		if(target->is_affected_by_effect(EFFECT_REVIVE_LIMIT) && !target->is_status(STATUS_PROC_COMPLETE)) {
			if((!nolimit && (target->current.location & 0x38)) || (!nocheck && !nolimit && (target->current.location & 0x3))) {
				arg.step = 4;
				return FALSE;
			}
		}
		///////////kdiy//////////
		//if((target->current.location == LOCATION_MZONE)
		if(((target->current.location == LOCATION_MZONE && !target->is_affected_by_effect(EFFECT_SANCT_MZONE)) 
			|| (target->current.location == LOCATION_SZONE && target->is_affected_by_effect(EFFECT_ORICA_SZONE)))
		//////////kdiy//////////
			|| (!(positions & POS_FACEDOWN) && check_unique_onfield(target, playerid, LOCATION_MZONE))
			|| !is_player_can_spsummon(core.reason_effect, target->summon.type & 0xff00ffff, positions, target->summon.player, playerid, target)
			|| (!nocheck && !(target->data.type & TYPE_MONSTER))) {
			arg.step = 4;
			return FALSE;
		}
		if(get_useable_count(target, playerid, LOCATION_MZONE, target->summon.player, LOCATION_REASON_TOFIELD, zone) <= 0) {
			if(target->current.location != LOCATION_GRAVE)
				core.ss_tograve_set.insert(target);
			arg.step = 4;
			return FALSE;
		}
		if(!nocheck) {
			effect_set eset;
			target->filter_effect(EFFECT_SPSUMMON_CONDITION, &eset);
			for(const auto& peff : eset) {
				pduel->lua->add_param<LuaParam::EFFECT>(core.reason_effect);
				pduel->lua->add_param<LuaParam::INT>(target->summon.player);
				pduel->lua->add_param<LuaParam::INT>(target->summon.type & 0xff00ffff);
				pduel->lua->add_param<LuaParam::INT>(positions);
				pduel->lua->add_param<LuaParam::INT>(playerid);
				if(!peff->check_value_condition(5)) {
					arg.step = 4;
					return FALSE;
				}
			}
		}
		arg.spsummon_cost_effects.clear();
		target->filter_effect(EFFECT_SPSUMMON_COST, &arg.spsummon_cost_effects);
		for(const auto& peffect : arg.spsummon_cost_effects) {
			if(peffect->operation) {
				core.sub_solving_event.push_back(nil_event);
				emplace_process<Processors::ExecuteOperation>(peffect, target->summon.player);
			}
		}
		return FALSE;
	}
	case 1: {
		for(const auto& peffect : arg.spsummon_cost_effects)
			release_oath_relation(peffect);
		if(!targets)
			core.special_summoning.insert(target);
		target->enable_field_effect(false);
		if(is_flag(DUEL_CANNOT_SUMMON_OATH_OLD)) {
			check_card_counter(target, ACTIVITY_SPSUMMON, target->summon.player);
		}
		// UNUSED VARIABLE	
		// uint32_t move_player = (target->data.type & TYPE_TOKEN) ? target->owner : target->summon_player;
		bool extra = !(zone & 0xff);
		if(targets && is_flag(DUEL_EMZONE)) {
			uint32_t flag1, flag2;
			int32_t ct1 = get_tofield_count(target, playerid, LOCATION_MZONE, target->summon.player, LOCATION_REASON_TOFIELD, zone, &flag1);
			int32_t ct2 = get_spsummonable_count_fromex(target, playerid, target->summon.player, zone, &flag2);
			for(auto& pcard : targets->container) {
				///////////kdiy//////////
				//if(pcard->current.location == LOCATION_MZONE)
				if((pcard->current.location == LOCATION_MZONE && !pcard->is_affected_by_effect(EFFECT_SANCT_MZONE)) || (pcard->current.location == LOCATION_SZONE && pcard->is_affected_by_effect(EFFECT_ORICA_SZONE)))
				//////////kdiy//////////
					continue;
				if(pcard->current.location != LOCATION_EXTRA)
					--ct1;
				else
					--ct2;
			}
			if(target->current.location != LOCATION_EXTRA) {
				if(ct2 == 0) {
					zone &= flag2;
					if(!extra)
						zone &= ~0x60;
				}
			} else {
				if(ct1 == 0)
					zone &= flag1;
			}
		}
	    ///////kdiy///////
		target->prev_temp.location = target->current.location;
		if(target->current.location == LOCATION_SZONE && target->is_affected_by_effect(EFFECT_ORICA_SZONE))
		    target->prev_temp.location = LOCATION_MZONE;
		if(target->current.location == LOCATION_MZONE && target->is_affected_by_effect(EFFECT_SANCT_MZONE))
		    target->prev_temp.location = LOCATION_SZONE;
		effect* oeffect = is_player_affected_by_effect(playerid,EFFECT_ORICA);	
	    if(oeffect && !target->is_affected_by_effect(EFFECT_ORICA_SZONE)) {
		    effect* deffect = pduel->new_effect();
			deffect->owner = oeffect->owner;
			deffect->code = EFFECT_ORICA_SZONE;
			deffect->type = EFFECT_TYPE_SINGLE;
			deffect->flag[0] = EFFECT_FLAG_CANNOT_DISABLE | EFFECT_FLAG_IGNORE_IMMUNE | EFFECT_FLAG_UNCOPYABLE | EFFECT_FLAG_OWNER_RELATE;
			deffect->reset_flag = RESET_EVENT+0x1fe0000+RESET_CONTROL-RESET_TURN_SET;
			target->add_effect(deffect);
            target->reset(EFFECT_SANCT_MZONE, RESET_CODE);
		}
		///////kdiy///////
		move_to_field(target, target->summon.player, playerid, LOCATION_MZONE, positions, FALSE, 0, zone);
		return FALSE;
	}
	case 2: {			
		auto message = pduel->new_message(MSG_SPSUMMONING);
		message->write<uint32_t>(target->data.code);
		message->write(target->get_info_location());
        /////kdiy///////
        message->write<uint32_t>(target->summon.type);
        message->write<uint8_t>(target->summon.player);
		message->write<bool>(target->single_effect.count((1295111 & 0xfffffff) | 0x10000000) > 0);
        /////kdiy///////
		return FALSE;
	}
	case 3: {
		returns.set<int32_t>(0, TRUE);
		set_control(target, target->current.controler, 0, 0);
		target->set_status(STATUS_SPSUMMON_STEP, TRUE);
		return TRUE;
	}
	case 5: {
		returns.set<int32_t>(0, FALSE);
		target->current.reason = target->temp.reason;
		target->current.reason_effect = target->temp.reason_effect;
		target->current.reason_player = target->temp.reason_player;
		if(targets)
			targets->container.erase(target);
		return TRUE;
	}
	}
	return TRUE;
}
bool field::process(Processors::SpSummon& arg) {
	auto  reason_effect = arg.reason_effect;
	auto reason_player = arg.reason_player;
	auto targets = arg.targets;
	auto zone = arg.zone;
	switch(arg.step) {
	case 0: {
		card_vector cvs, cvo;
		for(auto& pcard : targets->container) {
			if(pcard->summon.player == infos.turn_player)
				cvs.push_back(pcard);
			else
				cvo.push_back(pcard);
		}
		if(!cvs.empty()) {
			if(cvs.size() > 1)
				std::sort(cvs.begin(), cvs.end(), card::card_operation_sort);
			core.hint_timing[infos.turn_player] |= TIMING_SPSUMMON;
			for(auto& pcard : cvs)
				emplace_process<Processors::SpSummonStep>(targets, pcard, zone);
		}
		if(!cvo.empty()) {
			if(cvo.size() > 1)
				std::sort(cvo.begin(), cvo.end(), card::card_operation_sort);
			core.hint_timing[1 - infos.turn_player] |= TIMING_SPSUMMON;
			for(auto& pcard : cvo)
				emplace_process<Processors::SpSummonStep>(targets, pcard, zone);
		}
		return FALSE;
	}
	case 1: {
		if(core.ss_tograve_set.size())
			send_to(core.ss_tograve_set, nullptr, REASON_RULE, PLAYER_NONE, PLAYER_NONE, LOCATION_GRAVE, 0, POS_FACEUP);
		return FALSE;
	}
	case 2: {
		core.ss_tograve_set.clear();
		if(targets->container.size() == 0) {
			returns.set<int32_t>(0, 0);
			core.operated_set.clear();
			return TRUE;
		}
		bool tp = false, ntp = false;
		std::set<uint32_t> spsummon_once_set[2];
		for(auto& pcard : targets->container) {
			if(pcard->summon.player == infos.turn_player)
				tp = true;
			else
				ntp = true;
			if(pcard->spsummon_code)
				spsummon_once_set[pcard->summon.player].insert(pcard->spsummon_code);
		}
		if(tp)
			set_spsummon_counter(infos.turn_player);
		if(ntp)
			set_spsummon_counter(1 - infos.turn_player);
		for(auto& cit : spsummon_once_set[0])
			++core.spsummon_once_map[0][cit];
		for(auto& cit : spsummon_once_set[1])
			++core.spsummon_once_map[1][cit];
		for(auto& pcard : targets->container) {
			pcard->set_status(STATUS_SPSUMMON_STEP, FALSE);
			pcard->set_status(STATUS_SPSUMMON_TURN, TRUE);
			if(pcard->is_position(POS_FACEUP))
				pcard->enable_field_effect(true);
		}
		adjust_instant();
		return FALSE;
	}
	case 3: {
		pduel->new_message(MSG_SPSUMMONED);
		for(auto& pcard : targets->container) {
			if(!is_flag(DUEL_CANNOT_SUMMON_OATH_OLD)) {
				check_card_counter(pcard, ACTIVITY_SPSUMMON, pcard->summon.player);
			}
			if(!(pcard->current.position & POS_FACEDOWN))
				raise_single_event(pcard, nullptr, EVENT_SPSUMMON_SUCCESS, pcard->current.reason_effect, 0, pcard->current.reason_player, pcard->summon.player, 0);
			int32_t summontype = pcard->summon.type & 0xff000000;
			if(summontype && pcard->material_cards.size() && !pcard->is_status(STATUS_FUTURE_FUSION)) {
				int32_t matreason = 0;
				if(summontype == SUMMON_TYPE_FUSION)
					matreason = REASON_FUSION;
				else if(summontype == SUMMON_TYPE_RITUAL)
					matreason = REASON_RITUAL;
				else if(summontype == SUMMON_TYPE_XYZ)
					matreason = REASON_XYZ;
				else if(summontype == SUMMON_TYPE_LINK)
					matreason = REASON_LINK;
				for(auto& mcard : pcard->material_cards)
					raise_single_event(mcard, &targets->container, EVENT_BE_MATERIAL, pcard->current.reason_effect, matreason, pcard->current.reason_player, pcard->summon.player, 0);
				raise_event(pcard->material_cards, EVENT_BE_MATERIAL, reason_effect, matreason, reason_player, pcard->summon.player, 0);
			}
			pcard->set_status(STATUS_FUTURE_FUSION, FALSE);
		}
		process_single_event();
		process_instant_event();
		return FALSE;
	}
	case 4: {
		////kdiy////////
		raise_event(targets->container, EVENT_PRESPSUMMON_SUCCESS, reason_effect, 0, reason_player, PLAYER_NONE, 0);
		process_instant_event();
		////kdiy////////
		raise_event(targets->container, EVENT_SPSUMMON_SUCCESS, reason_effect, 0, reason_player, PLAYER_NONE, 0);
		process_instant_event();
		return FALSE;
	}
	case 5: {
		core.operated_set.clear();
		core.operated_set = targets->container;
		returns.set<int32_t>(0, static_cast<int32_t>(targets->container.size()));
		return TRUE;
	}
	}
	return TRUE;
}
bool field::process(Processors::DestroyReplace& arg) {
	auto targets = arg.targets;
	auto target = arg.target;
	auto battle = arg.battle;
	if(target->current.location & (LOCATION_GRAVE | LOCATION_REMOVED)) {
		target->current.reason = target->temp.reason;
		target->current.reason_effect = target->temp.reason_effect;
		target->current.reason_player = target->temp.reason_player;
		target->set_status(STATUS_DESTROY_CONFIRMED, FALSE);
		targets->container.erase(target);
		return TRUE;
	}
	if(!targets->has_card(target))
		return TRUE;
	returns.set<int32_t>(0, FALSE);
	effect_set eset;
	target->filter_single_continuous_effect(EFFECT_DESTROY_REPLACE, &eset);
	if(!battle) {
		for(const auto& peff : eset)
			emplace_process<Processors::OperationReplace>(peff, targets, target, true);
	} else {
		for(const auto& peff : eset)
			emplace_process<Processors::OperationReplace>(Step{ 10 }, peff, targets, target, true);
	}
	return TRUE;
}
bool field::process(Processors::Destroy& arg) {
	auto targets = arg.targets;
	auto reason_effect = arg.reason_effect;
	auto reason = arg.reason;
	auto reason_player = arg.reason_player;
	switch(arg.step) {
	case 0: {
		card_set extra;
		effect_set eset;
		card_set indestructable_set;
		std::set<effect*, effect_sort_by_id> indestructable_effect_set;
		for(auto cit = targets->container.begin(); cit != targets->container.end();) {
			auto rm = cit++;
			card* pcard = *rm;
			if(!pcard->is_destructable()) {
				indestructable_set.insert(pcard);
				continue;
			}
			////////kdiy//////////
			//if(!(pcard->current.reason & (REASON_RULE | REASON_COST))) {
			if((!(pcard->current.reason & REASON_COST))
			  && ((!(pcard->current.reason & REASON_RULE)) || (pcard->is_affected_by_effect(EFFECT_GOD_IMMUNE) && pcard->current.reason_effect && !pcard->current.reason_effect->owner->is_affected_by_effect(EFFECT_ULTIMATE_IMMUNE)))) {	
			////////kdiy//////////	
				bool is_destructable = true;
				if(!pcard->current.reason_effect || pcard->is_affect_by_effect(pcard->current.reason_effect)) {
					effect* indestructable_effect = pcard->check_indestructable_by_effect(pcard->current.reason_effect, pcard->current.reason_player);
					if(indestructable_effect) {
						if(reason_player != PLAYER_SELFDES)
							indestructable_effect_set.insert(indestructable_effect);
						is_destructable = false;
					}
				} else
					is_destructable = false;
				if(!is_destructable) {
					indestructable_set.insert(pcard);
					continue;
				}
			}
			eset.clear();
			pcard->filter_effect(EFFECT_INDESTRUCTABLE, &eset);
			if(eset.size()) {
				bool is_destructable = true;
				for(const auto& peff : eset) {
					pduel->lua->add_param<LuaParam::EFFECT>(pcard->current.reason_effect);
					pduel->lua->add_param<LuaParam::INT>(pcard->current.reason);
					pduel->lua->add_param<LuaParam::INT>(pcard->current.reason_player);
					if(peff->check_value_condition(3)) {
						if(reason_player != PLAYER_SELFDES)
							indestructable_effect_set.insert(peff);
						is_destructable = false;
						break;
					}
				}
				if(!is_destructable) {
					indestructable_set.insert(pcard);
					continue;
				}
			}
			eset.clear();
			pcard->filter_effect(EFFECT_INDESTRUCTABLE_COUNT, &eset);
			if(eset.size()) {
				bool is_destructable = true;
				for(const auto& peff : eset) {
					if(peff->is_flag(EFFECT_FLAG_COUNT_LIMIT)) {
						if(peff->count_limit == 0)
							continue;
						pduel->lua->add_param<LuaParam::EFFECT>(pcard->current.reason_effect);
						pduel->lua->add_param<LuaParam::INT>(pcard->current.reason);
						pduel->lua->add_param<LuaParam::INT>(pcard->current.reason_player);
						if(peff->check_value_condition(3)) {
							indestructable_effect_set.insert(peff);
							is_destructable = false;
						}
					} else {
						pduel->lua->add_param<LuaParam::EFFECT>(pcard->current.reason_effect);
						pduel->lua->add_param<LuaParam::INT>(pcard->current.reason);
						pduel->lua->add_param<LuaParam::INT>(pcard->current.reason_player);
						int32_t ct = peff->get_value(3);
						if(ct) {
							auto it = pcard->indestructable_effects.emplace(peff->id, 0);
							if(++it.first->second <= ct) {
								indestructable_effect_set.insert(peff);
								is_destructable = false;
							}
						}
					}
				}
				if(!is_destructable) {
					indestructable_set.insert(pcard);
					continue;
				}
			}
			eset.clear();
			pcard->filter_effect(EFFECT_DESTROY_SUBSTITUTE, &eset);
			if(eset.size()) {
				bool sub = false;
				for(const auto& peff : eset) {
					pduel->lua->add_param<LuaParam::EFFECT>(pcard->current.reason_effect);
					pduel->lua->add_param<LuaParam::INT>(pcard->current.reason);
					pduel->lua->add_param<LuaParam::INT>(pcard->current.reason_player);
					if(peff->check_value_condition(3)) {
						extra.insert(peff->handler);
						sub = true;
					}
				}
				if(sub) {
					pcard->current.reason = pcard->temp.reason;
					pcard->current.reason_effect = pcard->temp.reason_effect;
					pcard->current.reason_player = pcard->temp.reason_player;
					core.destroy_canceled.insert(pcard);
					targets->container.erase(pcard);
				}
			}
		}
		for (auto& pcard : indestructable_set) {
			pcard->current.reason = pcard->temp.reason;
			pcard->current.reason_effect = pcard->temp.reason_effect;
			pcard->current.reason_player = pcard->temp.reason_player;
			pcard->set_status(STATUS_DESTROY_CONFIRMED, FALSE);
			targets->container.erase(pcard);
		}
		for (auto& rep : extra) {
			if(!targets->has_card(rep)) {
				rep->temp.reason = rep->current.reason;
				rep->temp.reason_effect = rep->current.reason_effect;
				rep->temp.reason_player = rep->current.reason_player;
				rep->current.reason = REASON_EFFECT | REASON_DESTROY | REASON_REPLACE;
				rep->current.reason_effect = nullptr;
				rep->current.reason_player = rep->current.controler;
				rep->sendto_param.set(rep->owner, POS_FACEUP, LOCATION_GRAVE);
				targets->container.insert(rep);
			}
		}
		for (auto& peffect : indestructable_effect_set) {
			peffect->dec_count();
			auto message = pduel->new_message(MSG_HINT);
			message->write<uint8_t>(HINT_CARD);
			message->write<uint8_t>(0);
			message->write<uint64_t>(peffect->owner->data.code);
		}
		operation_replace(EFFECT_DESTROY_REPLACE, 5, targets);
		return FALSE;
	}
	case 1: {
		for (auto& pcard : targets->container) {
			emplace_process<Processors::DestroyReplace>(targets, pcard, false);
		}
		return FALSE;
	}
	case 2: {
		for (auto& pcard : core.destroy_canceled)
			pcard->set_status(STATUS_DESTROY_CONFIRMED, FALSE);
		core.destroy_canceled.clear();
		return FALSE;
	}
	case 3: {
		if(!targets->container.size()) {
			returns.set<int32_t>(0, 0);
			core.operated_set.clear();
			return TRUE;
		}
		card_vector cv(targets->container.begin(), targets->container.end());
		if(cv.size() > 1)
			std::sort(cv.begin(), cv.end(), card::card_operation_sort);
		for (auto& pcard : cv) {
			if(pcard->current.location & (LOCATION_GRAVE | LOCATION_REMOVED)) {
				pcard->current.reason = pcard->temp.reason;
				pcard->current.reason_effect = pcard->temp.reason_effect;
				pcard->current.reason_player = pcard->temp.reason_player;
				targets->container.erase(pcard);
				continue;
			}
			pcard->current.reason |= REASON_DESTROY;
			core.hint_timing[pcard->overlay_target ? pcard->overlay_target->current.controler : pcard->current.controler] |= TIMING_DESTROY;
			raise_single_event(pcard, nullptr, EVENT_DESTROY, pcard->current.reason_effect, pcard->current.reason, pcard->current.reason_player, 0, 0);
		}
		adjust_instant();
		process_single_event();
		raise_event(targets->container, EVENT_DESTROY, reason_effect, reason, reason_player, 0, 0);
		process_instant_event();
		return FALSE;
	}
	case 4: {
		auto sendtargets = pduel->new_group(targets->container);
		sendtargets->is_readonly = true;
		for(auto& pcard : sendtargets->container) {
			pcard->set_status(STATUS_DESTROY_CONFIRMED, FALSE);
			uint32_t dest = pcard->sendto_param.location;
			if(!dest)
				dest = LOCATION_GRAVE;
			if((dest == LOCATION_HAND && !pcard->is_capable_send_to_hand(reason_player))
			        || (dest == LOCATION_DECK && !pcard->is_capable_send_to_deck(reason_player))
			        || (dest == LOCATION_REMOVED && !pcard->is_removeable(reason_player, pcard->sendto_param.position, reason)))
				dest = LOCATION_GRAVE;
			pcard->sendto_param.location = dest;
		}
		operation_replace(EFFECT_SEND_REPLACE, 5, sendtargets);
		emplace_process<Processors::SendTo>(Step{ 1 }, sendtargets, reason_effect, reason | REASON_DESTROY, reason_player);
		return FALSE;
	}
	case 5: {
		core.operated_set.clear();
		core.operated_set = targets->container;
		for(auto cit = core.operated_set.begin(); cit != core.operated_set.end();) {
			if((*cit)->current.reason & REASON_REPLACE)
				core.operated_set.erase(cit++);
			else
				++cit;
		}
		returns.set<int32_t>(0, static_cast<int32_t>(core.operated_set.size()));
		return TRUE;
	}
	case 10: {
		effect_set eset;
		for(auto cit = targets->container.begin(); cit != targets->container.end();) {
			auto rm = cit++;
			card* pcard = *rm;
			if(!pcard->is_destructable()) {
				pcard->current.reason = pcard->temp.reason;
				pcard->current.reason_effect = pcard->temp.reason_effect;
				pcard->current.reason_player = pcard->temp.reason_player;
				pcard->set_status(STATUS_DESTROY_CONFIRMED, FALSE);
				targets->container.erase(pcard);
				continue;
			}
			eset.clear();
			pcard->filter_effect(EFFECT_INDESTRUCTABLE, &eset);
			if(eset.size()) {
				bool indes = false;
				for(const auto& peff : eset) {
					pduel->lua->add_param<LuaParam::EFFECT>(pcard->current.reason_effect);
					pduel->lua->add_param<LuaParam::INT>(pcard->current.reason);
					pduel->lua->add_param<LuaParam::INT>(pcard->current.reason_player);
					if(peff->check_value_condition(3)) {
						auto message = pduel->new_message(MSG_HINT);
						message->write<uint8_t>(HINT_CARD);
						message->write<uint8_t>(0);
						message->write<uint64_t>(peff->owner->data.code);
						indes = true;
						break;
					}
				}
				if(indes) {
					pcard->current.reason = pcard->temp.reason;
					pcard->current.reason_effect = pcard->temp.reason_effect;
					pcard->current.reason_player = pcard->temp.reason_player;
					pcard->set_status(STATUS_DESTROY_CONFIRMED, FALSE);
					targets->container.erase(pcard);
					continue;
				}
			}
			eset.clear();
			pcard->filter_effect(EFFECT_INDESTRUCTABLE_COUNT, &eset);
			if(eset.size()) {
				bool indes = false;
				for(const auto& peff : eset) {
					if(peff->is_flag(EFFECT_FLAG_COUNT_LIMIT)) {
						if(peff->count_limit == 0)
							continue;
						pduel->lua->add_param<LuaParam::EFFECT>(pcard->current.reason_effect);
						pduel->lua->add_param<LuaParam::INT>(pcard->current.reason);
						pduel->lua->add_param<LuaParam::INT>(pcard->current.reason_player);
						if(peff->check_value_condition(3)) {
							peff->dec_count();
							auto message = pduel->new_message(MSG_HINT);
							message->write<uint8_t>(HINT_CARD);
							message->write<uint8_t>(0);
							message->write<uint64_t>(peff->owner->data.code);
							indes = true;
						}
					} else {
						pduel->lua->add_param<LuaParam::EFFECT>(pcard->current.reason_effect);
						pduel->lua->add_param<LuaParam::INT>(pcard->current.reason);
						pduel->lua->add_param<LuaParam::INT>(pcard->current.reason_player);
						int32_t ct = peff->get_value(3);
						if(ct) {
							auto it = pcard->indestructable_effects.emplace(peff->id, 0);
							if(++it.first->second <= ct) {
								auto message = pduel->new_message(MSG_HINT);
								message->write<uint8_t>(HINT_CARD);
								message->write<uint8_t>(0);
								message->write<uint64_t>(peff->owner->data.code);
								indes = true;
							}
						}
					}
				}
				if(indes) {
					pcard->current.reason = pcard->temp.reason;
					pcard->current.reason_effect = pcard->temp.reason_effect;
					pcard->current.reason_player = pcard->temp.reason_player;
					pcard->set_status(STATUS_DESTROY_CONFIRMED, FALSE);
					targets->container.erase(pcard);
					continue;
				}
			}
			eset.clear();
			pcard->filter_effect(EFFECT_DESTROY_SUBSTITUTE, &eset);
			if(eset.size()) {
				bool sub = false;
				for(const auto& peff : eset) {
					pduel->lua->add_param<LuaParam::EFFECT>(pcard->current.reason_effect);
					pduel->lua->add_param<LuaParam::INT>(pcard->current.reason);
					pduel->lua->add_param<LuaParam::INT>(pcard->current.reason_player);
					if(peff->check_value_condition(3)) {
						core.battle_destroy_rep.insert(peff->handler);
						sub = true;
					}
				}
				if(sub) {
					pcard->current.reason = pcard->temp.reason;
					pcard->current.reason_effect = pcard->temp.reason_effect;
					pcard->current.reason_player = pcard->temp.reason_player;
					core.destroy_canceled.insert(pcard);
					targets->container.erase(pcard);
				}
			}
		}
		if(targets->container.size()) {
			operation_replace(EFFECT_DESTROY_REPLACE, 12, targets);
		}
		return FALSE;
	}
	case 11: {
		for (auto& pcard : targets->container) {
			emplace_process<Processors::DestroyReplace>(targets, pcard, true);
		}
		return FALSE;
	}
	case 12: {
		for (auto& pcard : core.destroy_canceled)
			pcard->set_status(STATUS_DESTROY_CONFIRMED, FALSE);
		core.destroy_canceled.clear();
		return TRUE;
	}
	}
	return TRUE;
}
bool field::process(Processors::ReleaseReplace& arg) {
	auto targets = arg.targets;
	auto target = arg.target;
	if(!(target->current.location & (LOCATION_ONFIELD | LOCATION_HAND))) {
		target->current.reason = target->temp.reason;
		target->current.reason_effect = target->temp.reason_effect;
		target->current.reason_player = target->temp.reason_player;
		targets->container.erase(target);
		return TRUE;
	}
	if(!targets->has_card(target))
		return TRUE;
	if(!(target->current.reason & REASON_RULE)) {
		returns.set<int32_t>(0, FALSE);
		effect_set eset;
		target->filter_single_continuous_effect(EFFECT_RELEASE_REPLACE, &eset);
		for(const auto& peff : eset)
			emplace_process<Processors::OperationReplace>(peff, targets, target, false);
	}
	return TRUE;
}
bool field::process(Processors::Release& arg) {
	auto targets = arg.targets;
	auto reason_effect = arg.reason_effect;
	auto reason = arg.reason;
	auto reason_player = arg.reason_player;
	switch(arg.step) {
	case 0: {
		for(auto cit = targets->container.begin(); cit != targets->container.end();) {
			auto rm = cit++;
			card* pcard = *rm;
			if(pcard->get_status(STATUS_SUMMONING | STATUS_SPSUMMON_STEP)
				|| ((reason & REASON_SUMMON) && !pcard->is_releasable_by_summon(reason_player, pcard->current.reason_card))
				////////kdiy//////////
				//|| (!(pcard->current.reason & (REASON_RULE | REASON_SUMMON | REASON_COST))
				|| (((!(pcard->current.reason & (REASON_SUMMON | REASON_COST)))
				  && ((!(pcard->current.reason & REASON_RULE)) || (pcard->is_affected_by_effect(EFFECT_GOD_IMMUNE) && pcard->current.reason_effect && !pcard->current.reason_effect->owner->is_affected_by_effect(EFFECT_ULTIMATE_IMMUNE))))
	            ////////kdiy//////////
					&& (!pcard->is_affect_by_effect(pcard->current.reason_effect) || !pcard->is_releasable_by_nonsummon(reason_player, reason)))) {
				pcard->current.reason = pcard->temp.reason;
				pcard->current.reason_effect = pcard->temp.reason_effect;
				pcard->current.reason_player = pcard->temp.reason_player;
				targets->container.erase(rm);
				continue;
			}
		}
		/////////kdiy////////
		//if(reason & REASON_RULE)
		if((reason & REASON_RULE) && targets->container.size() > 0)
		/////////kdiy////////
			return FALSE;
		operation_replace(EFFECT_RELEASE_REPLACE, 5, targets);
		return FALSE;
	}
	case 1: {
		for (auto& pcard : targets->container) {
			emplace_process<Processors::ReleaseReplace>(targets, pcard);
		}
		return FALSE;
	}
	case 2: {
		if(!targets->container.size()) {
			returns.set<int32_t>(0, 0);
			core.operated_set.clear();
			return TRUE;
		}
		card_vector cv(targets->container.begin(), targets->container.end());
		if(cv.size() > 1)
			std::sort(cv.begin(), cv.end(), card::card_operation_sort);
		for (auto& pcard : cv) {
			if(!(pcard->current.location & (LOCATION_ONFIELD | LOCATION_HAND))) {
				pcard->current.reason = pcard->temp.reason;
				pcard->current.reason_effect = pcard->temp.reason_effect;
				pcard->current.reason_player = pcard->temp.reason_player;
				targets->container.erase(pcard);
				continue;
			}
			pcard->current.reason |= REASON_RELEASE;
		}
		adjust_instant();
		return FALSE;
	}
	case 3: {
		for(auto& peffect : core.dec_count_reserve) {
			auto message = pduel->new_message(MSG_HINT);
			message->write<uint8_t>(HINT_CARD);
			message->write<uint8_t>(0);
			message->write<uint64_t>(peffect->get_handler()->data.code);
		}
		auto sendtargets = pduel->new_group(targets->container);
		sendtargets->is_readonly = true;
		operation_replace(EFFECT_SEND_REPLACE, 5, sendtargets);
		emplace_process<Processors::SendTo>(Step{ 1 }, sendtargets, reason_effect, reason | REASON_RELEASE, reason_player);
		return FALSE;
	}
	case 4: {
		for(auto& peffect : core.dec_count_reserve)
			peffect->dec_count();
		core.dec_count_reserve.clear();
		core.operated_set.clear();
		core.operated_set = targets->container;
		returns.set<int32_t>(0, static_cast<int32_t>(targets->container.size()));
		return TRUE;
	}
	}
	return TRUE;
}
bool field::process(Processors::SendToReplace& arg) {
	auto targets = arg.targets;
	auto target = arg.target;
	uint8_t playerid = target->sendto_param.playerid;
	uint8_t dest = target->sendto_param.location;
	if(!targets->has_card(target))
		return TRUE;
	if(target->current.location == dest && target->current.controler == playerid) {
		target->current.reason = target->temp.reason;
		target->current.reason_effect = target->temp.reason_effect;
		target->current.reason_player = target->temp.reason_player;
		targets->container.erase(target);
		return TRUE;
	}
	if(!(target->current.reason & REASON_RULE)) {
		returns.set<int32_t>(0, FALSE);
		effect_set eset;
		target->filter_single_continuous_effect(EFFECT_SEND_REPLACE, &eset);
		for(const auto& peff : eset)
			emplace_process<Processors::OperationReplace>(peff, targets, target, false);
	}
	return TRUE;
}
bool field::process(Processors::SendTo& arg) {
	auto targets = arg.targets;
	auto reason_effect = arg.reason_effect;
	auto reason = arg.reason;
	auto reason_player = arg.reason_player;
	switch(arg.step) {
	case 0: {
		for(auto cit = targets->container.begin(); cit != targets->container.end();) {
			auto rm = cit++;
			card* pcard = *rm;
			uint8_t dest = pcard->sendto_param.location;
			/////////kdiy////////
			//if(!(reason & REASON_RULE) &&
			if(((!(reason & REASON_RULE)) || (pcard->is_affected_by_effect(EFFECT_GOD_IMMUNE) && pcard->current.reason_effect && !pcard->current.reason_effect->owner->is_affected_by_effect(EFFECT_ULTIMATE_IMMUNE))) &&
			/////////kdiy////////
				(pcard->get_status(STATUS_SUMMONING | STATUS_SPSUMMON_STEP)
					|| (!(pcard->current.reason & (REASON_COST | REASON_SUMMON | REASON_MATERIAL)) && !pcard->is_affect_by_effect(pcard->current.reason_effect))
					|| (dest == LOCATION_HAND && !pcard->is_capable_send_to_hand(core.reason_player))
					|| (dest == LOCATION_DECK && !pcard->is_capable_send_to_deck(core.reason_player))
					|| (dest == LOCATION_REMOVED && !pcard->is_removeable(core.reason_player, pcard->sendto_param.position, reason))
					|| (dest == LOCATION_GRAVE && !pcard->is_capable_send_to_grave(core.reason_player))
					|| (dest == LOCATION_EXTRA && !pcard->is_capable_send_to_extra(core.reason_player)))) {
				pcard->current.reason = pcard->temp.reason;
				pcard->current.reason_player = pcard->temp.reason_player;
				pcard->current.reason_effect = pcard->temp.reason_effect;
				targets->container.erase(rm);
				continue;
			}
		}
		/////////kdiy////////
		//if(reason & REASON_RULE)
		if((reason & REASON_RULE) && targets->container.size() > 0)
		/////////kdiy////////
			return FALSE;
		operation_replace(EFFECT_SEND_REPLACE, 5, targets);
		return FALSE;
	}
	case 1: {
		for(auto& pcard : targets->container) {
			emplace_process<Processors::SendToReplace>(targets, pcard);
		}
		return FALSE;
	}
	case 2: {
		if(!targets->container.size()) {
			returns.set<int32_t>(0, 0);
			core.operated_set.clear();
			return TRUE;
		}
		card_set leave_p, destroying;
		for(auto& pcard : targets->container) {
			if((pcard->current.location & LOCATION_ONFIELD) && !pcard->is_status(STATUS_SUMMON_DISABLED) && !pcard->is_status(STATUS_ACTIVATE_DISABLED)) {
				raise_single_event(pcard, nullptr, EVENT_LEAVE_FIELD_P, pcard->current.reason_effect, pcard->current.reason, pcard->current.reason_player, 0, 0);
				leave_p.insert(pcard);
			}
			if((pcard->current.location & LOCATION_ONFIELD)) {
				if(pcard->current.position & POS_FACEUP) {
					pcard->previous.code = pcard->get_code();
					pcard->previous.code2 = pcard->get_another_code();
					pcard->previous.type = pcard->get_type();
					///////////kdiy//////////
					//if(pcard->current.location & LOCATION_MZONE) {
					if((pcard->current.location & LOCATION_MZONE && !pcard->is_affected_by_effect(EFFECT_SANCT_MZONE)) || (pcard->current.location & LOCATION_SZONE && pcard->is_affected_by_effect(EFFECT_ORICA_SZONE))) {
					//////////kdiy//////////
						pcard->previous.level = pcard->get_level();
						pcard->previous.rank = pcard->get_rank();
						pcard->previous.attribute = pcard->get_attribute();
						pcard->previous.race = pcard->get_race();
						pcard->previous.attack = pcard->get_attack();
						pcard->previous.defense = pcard->get_defense();
					}
				} else {
					effect_set eset;
					pcard->filter_effect(EFFECT_ADD_CODE, &eset);
					if(pcard->data.alias && !eset.size())
						pcard->previous.code = pcard->data.alias;
					else
						pcard->previous.code = pcard->data.code;
					if(eset.size())
						pcard->previous.code2 = eset.back()->get_value(pcard);
					else
						pcard->previous.code2 = 0;
					pcard->previous.type = pcard->data.type;
					pcard->previous.level = pcard->data.level;
					pcard->previous.rank = pcard->data.level;
					pcard->previous.attribute = pcard->data.attribute;
					pcard->previous.race = pcard->data.race;
					pcard->previous.attack = pcard->data.attack;
					pcard->previous.defense = pcard->data.defense;
				}
				effect_set eset;
				pcard->filter_effect(EFFECT_ADD_SETCODE, &eset);
				pcard->previous.setcodes.clear();
				for(auto& eff : eset) {
					pcard->previous.setcodes.insert((uint16_t)eff->get_value(pcard));
				}
			}
		}
		if(leave_p.size())
			raise_event(std::move(leave_p), EVENT_LEAVE_FIELD_P, reason_effect, reason, reason_player, 0, 0);
		if(destroying.size())
			raise_event(std::move(destroying), EVENT_DESTROY, reason_effect, reason, reason_player, 0, 0);
		process_single_event();
		process_instant_event();
		return FALSE;
	}
	case 3: {
		uint32_t dest, redirect, redirect_seq, check_cb;
		for(auto& pcard : targets->container)
			pcard->enable_field_effect(false);
		adjust_disable_check_list();
		for(auto& pcard : targets->container) {
			dest = pcard->sendto_param.location;
			redirect = 0;
			redirect_seq = 0;
			check_cb = 0;
			if((dest & LOCATION_GRAVE) && pcard->is_affected_by_effect(EFFECT_TO_GRAVE_REDIRECT_CB))
				check_cb = 1;
			if((pcard->current.location & LOCATION_ONFIELD) && !pcard->is_status(STATUS_SUMMON_DISABLED) && !pcard->is_status(STATUS_ACTIVATE_DISABLED)) {
				redirect = pcard->leave_field_redirect(pcard->current.reason);
				redirect_seq = redirect >> 16;
				redirect &= 0xffff;
			}
			if(redirect) {
				pcard->current.reason &= ~REASON_TEMPORARY;
				pcard->current.reason |= REASON_REDIRECT;
				pcard->sendto_param.location = redirect;
				pcard->sendto_param.sequence = redirect_seq;
				dest = redirect;
				if(dest == LOCATION_REMOVED) {
					if(pcard->sendto_param.position & POS_FACEDOWN_ATTACK)
						pcard->sendto_param.position = (pcard->sendto_param.position & ~POS_FACEDOWN_ATTACK) | POS_FACEUP_ATTACK;
					if(pcard->sendto_param.position & POS_FACEDOWN_DEFENSE)
						pcard->sendto_param.position = (pcard->sendto_param.position & ~POS_FACEDOWN_DEFENSE) | POS_FACEUP_DEFENSE;
				}
			}
			redirect = pcard->destination_redirect(dest, pcard->current.reason);
			if(redirect) {
				redirect_seq = redirect >> 16;
				redirect &= 0xffff;
			}
			if(redirect && (pcard->current.location != redirect)) {
				pcard->current.reason |= REASON_REDIRECT;
				pcard->sendto_param.location = redirect;
				pcard->sendto_param.sequence = redirect_seq;
			}
			if(check_cb)
				pcard->sendto_param.playerid |= 0x1u << 4;
		}
		return FALSE;
	}
	case 4: {
		arg.extra_args = std::make_unique<Processors::SendTo::exargs>();
		auto& param = arg.extra_args;
		param->show_decktop[0] = false;
		param->show_decktop[1] = false;
		param->cv.assign(targets->container.begin(), targets->container.end());
		if(param->cv.size() > 1)
			std::sort(param->cv.begin(), param->cv.end(), card::card_operation_sort);
		if(core.global_flag & GLOBALFLAG_DECK_REVERSE_CHECK) {
			int32_t d0 = static_cast<int32_t>(player[0].list_main.size() - 1), s0 = d0;
			int32_t d1 = static_cast<int32_t>(player[1].list_main.size() - 1), s1 = d1;
			for(auto& pcard : param->cv) {
				if(pcard->current.location != LOCATION_DECK)
					continue;
				if((pcard->current.controler == 0) && (pcard->current.sequence == static_cast<uint32_t>(s0)))
					--s0;
				if((pcard->current.controler == 1) && (pcard->current.sequence == static_cast<uint32_t>(s1)))
					--s1;
			}
			if((s0 != d0) && (s0 > 0)) {
				card* ptop = player[0].list_main[s0];
				if(core.deck_reversed || (ptop->current.position == POS_FACEUP_DEFENSE)) {
					auto message = pduel->new_message(MSG_DECK_TOP);
					message->write<uint8_t>(0);
					message->write<uint32_t>(static_cast<uint32_t>(d0 - s0));
					message->write<uint32_t>(ptop->data.code);
					message->write<uint32_t>(ptop->current.position);
				}
			}
			if((s1 != d1) && (s1 > 0)) {
				card* ptop = player[1].list_main[s1];
				if(core.deck_reversed || (ptop->current.position == POS_FACEUP_DEFENSE)) {
					auto message = pduel->new_message(MSG_DECK_TOP);
					message->write<uint8_t>(1);
					message->write<uint32_t>(d1 - s1);
					message->write<uint32_t>(ptop->data.code);
					message->write<uint32_t>(ptop->current.position);
				}
			}
		}
		param->cvit = param->cv.begin();
		return FALSE;
	}
	case 5: {
		auto& param = arg.extra_args;
		if(param->cvit == param->cv.end()) {
			arg.step = 8;
			return FALSE;
		}
		card* pcard = *param->cvit;
		param->predirect = nullptr;
		uint32_t check_cb = pcard->sendto_param.playerid >> 4;
		if(check_cb)
			param->predirect = pcard->is_affected_by_effect(EFFECT_TO_GRAVE_REDIRECT_CB);
		pcard->enable_field_effect(false);
		if(pcard->data.type & TYPE_TOKEN) {
			auto message = pduel->new_message(MSG_MOVE);
			message->write<uint32_t>(pcard->data.code);
			message->write(pcard->get_info_location());
			message->write(loc_info{});
			message->write<uint32_t>(pcard->current.reason);
            ///kdiy///////////
			message->write<uint8_t>(pcard->current.reason_player);
            message->write<bool>(false);
            message->write<bool>(param->cvit == param->cv.begin());
            message->write<bool>(false);
            ///kdiy///////////
			if(core.current_chain.size() > 0)
				core.just_sent_cards.insert(pcard);
			pcard->previous.controler = pcard->current.controler;
			pcard->previous.location = pcard->current.location;
			pcard->previous.sequence = pcard->current.sequence;
			pcard->previous.position = pcard->current.position;
			pcard->previous.pzone = pcard->current.pzone;
			pcard->current.reason &= ~REASON_TEMPORARY;
			pcard->fieldid = infos.field_id++;
			pcard->fieldid_r = pcard->fieldid;
			pcard->reset(RESET_LEAVE, RESET_EVENT);
			pcard->clear_relate_effect();
			remove_card(pcard);
			param->leave_field.insert(pcard);
			++param->cvit;
			arg.step = 4;
			pcard->set_status(STATUS_LEAVE_CONFIRMED, FALSE);
			return FALSE;
		}
		if(param->predirect && get_useable_count(pcard, pcard->current.controler, LOCATION_SZONE, pcard->current.controler, LOCATION_REASON_TOFIELD) > 0)
			emplace_process<Processors::SelectEffectYesNo>(pcard->current.controler, 97, pcard);
		else
			returns.set<int32_t>(0, 0);
		return FALSE;
	}
	case 6: {
		if(returns.at<int32_t>(0))
			return FALSE;
		auto& param = arg.extra_args;
		card* pcard = *param->cvit;
		uint8_t oloc = pcard->current.location;
		uint8_t playerid = pcard->sendto_param.playerid & 0x7;
		uint8_t dest = pcard->sendto_param.location;
		uint8_t seq = pcard->sendto_param.sequence;
		uint8_t control_player = pcard->overlay_target ? pcard->overlay_target->current.controler : pcard->current.controler;
		if(dest == LOCATION_GRAVE) {
			core.hint_timing[control_player] |= TIMING_TOGRAVE;
		} else if(dest == LOCATION_HAND) {
			pcard->set_status(STATUS_PROC_COMPLETE, FALSE);
			core.hint_timing[control_player] |= TIMING_TOHAND;
		} else if(dest == LOCATION_DECK) {
			pcard->set_status(STATUS_PROC_COMPLETE, FALSE);
			core.hint_timing[control_player] |= TIMING_TODECK;
		} else if(dest == LOCATION_REMOVED) {
			core.hint_timing[control_player] |= TIMING_REMOVE;
		}
		//call move_card()
		if(pcard->current.controler != playerid || pcard->current.location != dest) {
			auto message = pduel->new_message(MSG_MOVE);
			message->write<uint32_t>(pcard->data.code);
			message->write(pcard->get_info_location());
			if(pcard->overlay_target) {
				param->detach.insert(pcard->overlay_target);
				pcard->overlay_target->xyz_remove(pcard);
			}
			move_card(playerid, pcard, dest, seq);
			pcard->current.position = pcard->sendto_param.position;
			message->write(pcard->get_info_location());
			message->write<uint32_t>(pcard->current.reason);
            ///kdiy///////////
			message->write<uint8_t>(pcard->current.reason_player);
            message->write<bool>(false);
            message->write<bool>(param->cvit == param->cv.begin());
            message->write<bool>((dest == LOCATION_SZONE && !pcard->is_affected_by_effect(EFFECT_ORICA_SZONE)) || pcard->is_affected_by_effect(EFFECT_SANCT_MZONE));
            ///kdiy///////////
		}
		if((core.deck_reversed && pcard->current.location == LOCATION_DECK) || (pcard->current.position == POS_FACEUP_DEFENSE))
			param->show_decktop[control_player] = true;
		pcard->set_status(STATUS_LEAVE_CONFIRMED, FALSE);
		if(pcard->status & (STATUS_SUMMON_DISABLED | STATUS_ACTIVATE_DISABLED)) {
			pcard->set_status(STATUS_SUMMON_DISABLED | STATUS_ACTIVATE_DISABLED, FALSE);
			pcard->previous.location = 0;
		} else if(oloc & LOCATION_ONFIELD) {
			pcard->reset(RESET_LEAVE, RESET_EVENT);
			param->leave_field.insert(pcard);
		} else if(oloc == LOCATION_GRAVE) {
			param->leave_grave.insert(pcard);
		}
		if(pcard->previous.location == LOCATION_OVERLAY)
			pcard->previous.controler = control_player;
		++param->cvit;
		arg.step = 4;
		return FALSE;
	}
	case 7: {
		// crystal beast redirection
		auto& param = arg.extra_args;
		card* pcard = *param->cvit;
		uint32_t flag;
		get_useable_count(pcard, pcard->current.controler, LOCATION_SZONE, pcard->current.controler, LOCATION_REASON_TOFIELD, 0xff, &flag);
		/////kdiy////
		//flag = ((flag << 8) & 0xff00) | 0xffffe0ff;
        if(is_player_affected_by_effect(pcard->current.controler, EFFECT_SANCT))
			flag = ((flag & 0xff1f)) | 0xffffe0e0;
        else
			flag = ((flag & 0xff00)) | 0xffffe0ff;
		/////kdiy////
		auto message = pduel->new_message(MSG_HINT);
		message->write<uint8_t>(HINT_SELECTMSG);
		message->write<uint8_t>(pcard->current.controler);
		message->write<uint64_t>(pcard->data.code);
		emplace_process<Processors::SelectPlace>(pcard->current.controler, flag, 1);
		return FALSE;
	}
	case 8: {
		auto& param = arg.extra_args;
		card* pcard = *param->cvit;
		uint8_t oloc = pcard->current.location;
		//kdiy///////
		pcard->temp.location = returns.at<int8_t>(1);
		//kdiy///////
		uint8_t seq = returns.at<int8_t>(2);
		auto message = pduel->new_message(MSG_MOVE);
		message->write<uint32_t>(pcard->data.code);
		message->write(pcard->get_info_location());
		if(pcard->overlay_target) {
			param->detach.insert(pcard->overlay_target);
			pcard->overlay_target->xyz_remove(pcard);
		}
		//kdiy///////
		effect* seffect = is_player_affected_by_effect(pcard->current.controler, EFFECT_SANCT);	
		if(seffect && !pcard->is_affected_by_effect(EFFECT_SANCT_MZONE)) {
			effect* deffect = pduel->new_effect();
			deffect->owner = seffect->owner;
			deffect->code = EFFECT_SANCT_MZONE;
			deffect->type = EFFECT_TYPE_SINGLE;
			deffect->flag[0] = EFFECT_FLAG_CANNOT_DISABLE | EFFECT_FLAG_IGNORE_IMMUNE | EFFECT_FLAG_UNCOPYABLE;
			deffect->reset_flag = RESET_EVENT+0x1fe0000+RESET_CONTROL-RESET_TURN_SET;
			pcard->add_effect(deffect);
            pcard->reset(EFFECT_ORICA_SZONE, RESET_CODE);
		}
        int8_t location2 = pcard->temp.location;
		if(pcard->temp.location == LOCATION_MZONE || pcard->temp.location == LOCATION_SZONE)
		move_card(pcard->current.controler, pcard, pcard->temp.location, seq);
		else
		//kdiy///////
		move_card(pcard->current.controler, pcard, LOCATION_SZONE, seq);
		pcard->current.position = POS_FACEUP;
		message->write(pcard->get_info_location());
		message->write<uint32_t>(pcard->current.reason);
        ///kdiy///////////
		message->write<uint8_t>(pcard->current.reason_player);
        message->write<bool>(false);
        message->write<bool>(param->cvit == param->cv.begin());
        message->write<bool>(true);
		pcard->temp.location = 0;
        ///kdiy///////////
		pcard->set_status(STATUS_LEAVE_CONFIRMED, FALSE);
		if(pcard->status & (STATUS_SUMMON_DISABLED | STATUS_ACTIVATE_DISABLED)) {
			pcard->set_status(STATUS_SUMMON_DISABLED | STATUS_ACTIVATE_DISABLED, FALSE);
			pcard->previous.location = 0;
		} else if(oloc & LOCATION_ONFIELD) {
			pcard->reset(RESET_LEAVE + RESET_MSCHANGE, RESET_EVENT);
			pcard->clear_card_target();
			param->leave_field.insert(pcard);
		}
        //kdiy///////
		if(seffect && !pcard->is_affected_by_effect(EFFECT_SANCT_MZONE)) {
			effect* deffect = pduel->new_effect();
			deffect->owner = seffect->owner;
			deffect->code = EFFECT_SANCT_MZONE;
			deffect->type = EFFECT_TYPE_SINGLE;
			deffect->flag[0] = EFFECT_FLAG_CANNOT_DISABLE | EFFECT_FLAG_IGNORE_IMMUNE | EFFECT_FLAG_UNCOPYABLE;
			deffect->reset_flag = RESET_EVENT+0x1fe0000+RESET_CONTROL-RESET_TURN_SET;
			pcard->add_effect(deffect);
            pcard->reset(EFFECT_ORICA_SZONE, RESET_CODE);
		}
        //kdiy///////
		if(param->predirect->operation) {
			tevent e{};
			e.event_cards = targets;
			e.event_player = pcard->current.controler;
			e.event_value = 0;
			e.reason = pcard->current.reason;
			e.reason_effect = reason_effect;
			e.reason_player = pcard->current.controler;
			core.sub_solving_event.push_back(e);
			emplace_process<Processors::ExecuteOperation>(param->predirect, pcard->current.controler);
		}
		++param->cvit;
		arg.step = 4;
		return FALSE;
	}
	case 9: {
		auto& param = arg.extra_args;
		if(core.global_flag & GLOBALFLAG_DECK_REVERSE_CHECK) {
			if(param->show_decktop[0]) {
				card* ptop = *player[0].list_main.rbegin();
				auto message = pduel->new_message(MSG_DECK_TOP);
				message->write<uint8_t>(0);
				message->write<uint32_t>(0);
				message->write<uint32_t>(ptop->data.code);
				message->write<uint32_t>(ptop->current.position);
			}
			if(param->show_decktop[1]) {
				card* ptop = *player[1].list_main.rbegin();
				auto message = pduel->new_message(MSG_DECK_TOP);
				message->write<uint8_t>(1);
				message->write<uint32_t>(0);
				message->write<uint32_t>(ptop->data.code);
				message->write<uint32_t>(ptop->current.position);
			}
		}
		for(auto& pcard : targets->container) {
			if(!(pcard->data.type & TYPE_TOKEN))
				pcard->enable_field_effect(true);
			uint8_t nloc = pcard->current.location;
			if(nloc == LOCATION_HAND)
				pcard->reset(RESET_TOHAND, RESET_EVENT);
			if(nloc == LOCATION_DECK || nloc == LOCATION_EXTRA)
				pcard->reset(RESET_TODECK, RESET_EVENT);
			if(nloc == LOCATION_GRAVE)
				pcard->reset(RESET_TOGRAVE, RESET_EVENT);
			if(nloc == LOCATION_REMOVED || ((pcard->data.type & TYPE_TOKEN) && pcard->sendto_param.location == LOCATION_REMOVED)) {
				if(pcard->current.reason & REASON_TEMPORARY)
					pcard->reset(RESET_TEMP_REMOVE, RESET_EVENT);
				else
					pcard->reset(RESET_REMOVE, RESET_EVENT);
			}
			pcard->refresh_disable_status();
		}
		for(auto& pcard : param->leave_field)
			raise_single_event(pcard, nullptr, EVENT_LEAVE_FIELD, pcard->current.reason_effect, pcard->current.reason, pcard->current.reason_player, 0, 0);
		for(auto& pcard : param->leave_grave)
			raise_single_event(pcard, nullptr, EVENT_LEAVE_GRAVE, pcard->current.reason_effect, pcard->current.reason, pcard->current.reason_player, 0, 0);
		if((core.global_flag & GLOBALFLAG_DETACH_EVENT) && param->detach.size()) {
			for(auto& pcard : param->detach) {
				///////////kdiy//////////
				//if(pcard->current.location & LOCATION_MZONE)
				if(pcard->current.location & LOCATION_ONFIELD)
				//////////kdiy//////////
					raise_single_event(pcard, nullptr, EVENT_DETACH_MATERIAL, reason_effect, reason, reason_player, 0, 0);
			}
		}
		process_single_event();
		if(param->leave_field.size())
			raise_event(std::move(param->leave_field), EVENT_LEAVE_FIELD, reason_effect, reason, reason_player, 0, 0);
		if(param->leave_grave.size())
			raise_event(std::move(param->leave_grave), EVENT_LEAVE_GRAVE, reason_effect, reason, reason_player, 0, 0);
		if((core.global_flag & GLOBALFLAG_DETACH_EVENT) && param->detach.size())
			raise_event(std::move(param->detach), EVENT_DETACH_MATERIAL, reason_effect, reason, reason_player, 0, 0);
		process_instant_event();
		adjust_instant();
		return FALSE;
	}
	case 10: {
		card_set tohand, todeck, tograve, remove, discard, released, destroyed;
		card_set equipings, overlays;
		for(auto& pcard : targets->container) {
			uint8_t nloc = pcard->current.location;
			if(pcard->equiping_target)
				pcard->unequip();
			if(pcard->equiping_cards.size()) {
				for(auto csit = pcard->equiping_cards.begin(); csit != pcard->equiping_cards.end();) {
					card* equipc = *(csit++);
					equipc->unequip();
					//kdiy//////////
					//if(equipc->current.location == LOCATION_SZONE)
					if(equipc->current.location == LOCATION_SZONE || equipc->current.location == LOCATION_MZONE)
					//kdiy//////////
						equipings.insert(equipc);
				}
			}
			pcard->clear_card_target();
			if(nloc == LOCATION_HAND) {
				if(pcard->owner != pcard->current.controler) {
					effect* deffect = pduel->new_effect();
					deffect->owner = pcard;
					deffect->code = 0;
					deffect->type = EFFECT_TYPE_SINGLE;
					deffect->flag[0] = EFFECT_FLAG_CANNOT_DISABLE | EFFECT_FLAG_CLIENT_HINT;
					deffect->description = 67;
					deffect->reset_flag = RESET_EVENT + 0x1fe0000;
					pcard->add_effect(deffect);
				}
				if(core.current_chain.size()) {
					// Added to the hand by a currently resolving effect
					effect* deffect = pduel->new_effect();
					deffect->owner = pcard;
					deffect->code = 0;
					deffect->type = EFFECT_TYPE_SINGLE;
					deffect->flag[0] = EFFECT_FLAG_CANNOT_DISABLE | EFFECT_FLAG_CLIENT_HINT;
					deffect->description = 225;
					deffect->reset_flag = (RESET_EVENT | RESET_TOFIELD | RESET_LEAVE | RESET_TODECK |
										   RESET_TOHAND | RESET_TEMP_REMOVE | RESET_REMOVE |
										   RESET_TOGRAVE | RESET_TURN_SET | RESET_CHAIN);
					pcard->add_effect(deffect);
				}
				tohand.insert(pcard);
				raise_single_event(pcard, nullptr, EVENT_TO_HAND, pcard->current.reason_effect, pcard->current.reason, pcard->current.reason_player, 0, 0);
			}
			if(nloc == LOCATION_DECK || nloc == LOCATION_EXTRA) {
				todeck.insert(pcard);
				raise_single_event(pcard, nullptr, EVENT_TO_DECK, pcard->current.reason_effect, pcard->current.reason, pcard->current.reason_player, 0, 0);
			}
			if(nloc == LOCATION_GRAVE && !(pcard->current.reason & REASON_RETURN)) {
				tograve.insert(pcard);
				raise_single_event(pcard, nullptr, EVENT_TO_GRAVE, pcard->current.reason_effect, pcard->current.reason, pcard->current.reason_player, 0, 0);
			}
			if(nloc == LOCATION_REMOVED || ((pcard->data.type & TYPE_TOKEN) && pcard->sendto_param.location == LOCATION_REMOVED)) {
				remove.insert(pcard);
				raise_single_event(pcard, nullptr, EVENT_REMOVE, pcard->current.reason_effect, pcard->current.reason, pcard->current.reason_player, 0, 0);
			}
			if(pcard->current.reason & REASON_DISCARD) {
				discard.insert(pcard);
				raise_single_event(pcard, nullptr, EVENT_DISCARD, pcard->current.reason_effect, pcard->current.reason, pcard->current.reason_player, 0, 0);
			}
			if(pcard->current.reason & REASON_RELEASE) {
				released.insert(pcard);
				raise_single_event(pcard, nullptr, EVENT_RELEASE, pcard->current.reason_effect, pcard->current.reason, pcard->current.reason_player, 0, 0);
			}
			// non-battle destroy
			if(pcard->current.reason & REASON_DESTROY && !(pcard->current.reason & REASON_BATTLE)) {
				destroyed.insert(pcard);
				raise_single_event(pcard, nullptr, EVENT_DESTROYED, pcard->current.reason_effect, pcard->current.reason, pcard->current.reason_player, 0, 0);
			}
			if(pcard->xyz_materials.size()) {
				for(auto& mcard : pcard->xyz_materials)
					overlays.insert(mcard);
			}
			raise_single_event(pcard, nullptr, EVENT_MOVE, pcard->current.reason_effect, pcard->current.reason, pcard->current.reason_player, 0, 0);
		}
		if(tohand.size())
			raise_event(std::move(tohand), EVENT_TO_HAND, reason_effect, reason, reason_player, 0, 0);
		if(todeck.size())
			raise_event(std::move(todeck), EVENT_TO_DECK, reason_effect, reason, reason_player, 0, 0);
		if(tograve.size())
			raise_event(std::move(tograve), EVENT_TO_GRAVE, reason_effect, reason, reason_player, 0, 0);
		if(remove.size())
			raise_event(std::move(remove), EVENT_REMOVE, reason_effect, reason, reason_player, 0, 0);
		if(discard.size())
			raise_event(std::move(discard), EVENT_DISCARD, reason_effect, reason, reason_player, 0, 0);
		if(released.size())
			raise_event(std::move(released), EVENT_RELEASE, reason_effect, reason, reason_player, 0, 0);
		if(destroyed.size())
			raise_event(std::move(destroyed), EVENT_DESTROYED, reason_effect, reason, reason_player, 0, 0);
		raise_event(targets->container, EVENT_MOVE, reason_effect, reason, reason_player, 0, 0);
		process_single_event();
		process_instant_event();
		if(equipings.size())
			destroy(std::move(equipings), nullptr, REASON_RULE + REASON_LOST_TARGET, PLAYER_NONE);
		if(overlays.size())
			send_to(std::move(overlays), nullptr, REASON_RULE + REASON_LOST_TARGET, PLAYER_NONE, PLAYER_NONE, LOCATION_GRAVE, 0, POS_FACEUP);
		adjust_instant();
		return FALSE;
	}
	case 11: {
		core.operated_set.clear();
		core.operated_set = targets->container;
		returns.set<int32_t>(0, static_cast<int32_t>(targets->container.size()));
		return TRUE;
	}
	}
	return TRUE;
}
bool field::process(Processors::DiscardDeck& arg) {
	auto playerid = arg.playerid;
	auto count = arg.count;
	auto reason = arg.reason;
	switch(arg.step) {
	case 0: {
		if(is_player_affected_by_effect(playerid, EFFECT_CANNOT_DISCARD_DECK)) {
			core.operated_set.clear();
			returns.set<int32_t>(0, 0);
			return TRUE;
		}
		int32_t i = 0;
		for(auto cit = player[playerid].list_main.rbegin(); i < count && cit != player[playerid].list_main.rend(); ++cit, ++i) {
			uint32_t dest = LOCATION_GRAVE;
			(*cit)->sendto_param.location = LOCATION_GRAVE;
			(*cit)->current.reason_effect = core.reason_effect;
			(*cit)->current.reason_player = core.reason_player;
			(*cit)->current.reason = reason;
			uint32_t redirect = (*cit)->destination_redirect(dest, reason) & 0xffff;
			if(redirect) {
				(*cit)->sendto_param.location = redirect;
			}
		}
		if(core.global_flag & GLOBALFLAG_DECK_REVERSE_CHECK) {
			if(player[playerid].list_main.size() > count) {
				card* ptop = *(player[playerid].list_main.rbegin() + count);
				if(core.deck_reversed || (ptop->current.position == POS_FACEUP_DEFENSE)) {
					auto message = pduel->new_message(MSG_DECK_TOP);
					message->write<uint8_t>(playerid);
					message->write<uint32_t>(count);
					message->write<uint32_t>(ptop->data.code);
					message->write<uint32_t>(ptop->current.position);
				}
			}
		}
		return FALSE;
	}
	case 1: {
		card_set tohand, todeck, tograve, remove;
		core.discarded_set.clear();
		for(int32_t i = 0; i < count; ++i) {
			if(player[playerid].list_main.size() == 0)
				break;
			card* pcard = player[playerid].list_main.back();
			uint8_t dest = pcard->sendto_param.location;
			if(dest == LOCATION_GRAVE)
				pcard->reset(RESET_TOGRAVE, RESET_EVENT);
			else if(dest == LOCATION_HAND) {
				pcard->reset(RESET_TOHAND, RESET_EVENT);
				pcard->set_status(STATUS_PROC_COMPLETE, FALSE);
			} else if(dest == LOCATION_DECK) {
				pcard->reset(RESET_TODECK, RESET_EVENT);
				pcard->set_status(STATUS_PROC_COMPLETE, FALSE);
			} else if(dest == LOCATION_REMOVED) {
				if(pcard->current.reason & REASON_TEMPORARY)
					pcard->reset(RESET_TEMP_REMOVE, RESET_EVENT);
				else
					pcard->reset(RESET_REMOVE, RESET_EVENT);
			}
			auto message = pduel->new_message(MSG_MOVE);
			message->write<uint32_t>(pcard->data.code);
			message->write(pcard->get_info_location());
			pcard->enable_field_effect(false);
			pcard->cancel_field_effect();
			player[playerid].list_main.pop_back();
			if(core.current_chain.size() > 0)
				core.just_sent_cards.insert(pcard);
			pcard->previous.controler = pcard->current.controler;
			pcard->previous.location = pcard->current.location;
			pcard->previous.sequence = pcard->current.sequence;
			pcard->previous.position = pcard->current.position;
			pcard->previous.pzone = pcard->current.pzone;
			pcard->current.controler = PLAYER_NONE;
			pcard->current.location = 0;
			add_card(pcard->owner, pcard, dest, 0);
			pcard->enable_field_effect(true);
			pcard->current.position = POS_FACEUP;
			message->write(pcard->get_info_location());
			message->write<uint32_t>(pcard->current.reason);
            ///kdiy///////////
			message->write<uint8_t>(pcard->current.reason_player);
            message->write<bool>(false);
            message->write<bool>(i == 0);
            message->write<bool>(false);
            ///kdiy///////////
			if(dest == LOCATION_HAND) {
				if(pcard->owner != pcard->current.controler) {
					effect* deffect = pduel->new_effect();
					deffect->owner = pcard;
					deffect->code = 0;
					deffect->type = EFFECT_TYPE_SINGLE;
					deffect->flag[0] = EFFECT_FLAG_CANNOT_DISABLE | EFFECT_FLAG_CLIENT_HINT;
					deffect->description = 67;
					deffect->reset_flag = RESET_EVENT + 0x1fe0000;
					pcard->add_effect(deffect);
				}
				if(core.current_chain.size()) {
					// Added to the hand by a currently resolving effect
					effect* deffect = pduel->new_effect();
					deffect->owner = pcard;
					deffect->code = 0;
					deffect->type = EFFECT_TYPE_SINGLE;
					deffect->flag[0] = EFFECT_FLAG_CANNOT_DISABLE | EFFECT_FLAG_CLIENT_HINT;
					deffect->description = 225;
					deffect->reset_flag = (RESET_EVENT | RESET_TOFIELD | RESET_LEAVE | RESET_TODECK |
										   RESET_TOHAND | RESET_TEMP_REMOVE | RESET_REMOVE |
										   RESET_TOGRAVE | RESET_TURN_SET | RESET_CHAIN);
					pcard->add_effect(deffect);
				}
				tohand.insert(pcard);
				raise_single_event(pcard, nullptr, EVENT_TO_HAND, pcard->current.reason_effect, pcard->current.reason, pcard->current.reason_player, 0, 0);
			} else if(dest == LOCATION_DECK || dest == LOCATION_EXTRA) {
				todeck.insert(pcard);
				raise_single_event(pcard, nullptr, EVENT_TO_DECK, pcard->current.reason_effect, pcard->current.reason, pcard->current.reason_player, 0, 0);
			} else if(dest == LOCATION_GRAVE) {
				tograve.insert(pcard);
				raise_single_event(pcard, nullptr, EVENT_TO_GRAVE, pcard->current.reason_effect, pcard->current.reason, pcard->current.reason_player, 0, 0);
			} else if(dest == LOCATION_REMOVED) {
				remove.insert(pcard);
				raise_single_event(pcard, nullptr, EVENT_REMOVE, pcard->current.reason_effect, pcard->current.reason, pcard->current.reason_player, 0, 0);
			}
			raise_single_event(pcard, nullptr, EVENT_MOVE, pcard->current.reason_effect, pcard->current.reason, pcard->current.reason_player, 0, 0);
			core.discarded_set.insert(pcard);
		}
		if(tohand.size())
			raise_event(std::move(tohand), EVENT_TO_HAND, core.reason_effect, reason, core.reason_player, 0, 0);
		if(todeck.size())
			raise_event(std::move(todeck), EVENT_TO_DECK, core.reason_effect, reason, core.reason_player, 0, 0);
		if(tograve.size())
			raise_event(std::move(tograve), EVENT_TO_GRAVE, core.reason_effect, reason, core.reason_player, 0, 0);
		if(remove.size())
			raise_event(std::move(remove), EVENT_REMOVE, core.reason_effect, reason, core.reason_player, 0, 0);
		raise_event(core.discarded_set, EVENT_MOVE, core.reason_effect, reason, core.reason_player, 0, 0);
		process_single_event();
		process_instant_event();
		adjust_instant();
		return FALSE;
	}
	case 2: {
		core.operated_set.swap(core.discarded_set);
		returns.set<int32_t>(0, static_cast<int32_t>(core.operated_set.size()));
		core.discarded_set.clear();
		return TRUE;
	}
	}
	return TRUE;
}
// move a card from anywhere to field, including sp_summon, Duel.MoveToField(), Duel.ReturnToField()
// ret: 0 = default, 1 = return after temporarily banished, 2 = trap_monster return to LOCATION_SZONE
// call move_card() in step 2
bool field::process(Processors::MoveToField& arg) {
	auto target = arg.target;
	auto enable = arg.enable;
	auto ret = arg.ret;
	auto pzone = arg.pzone;
	auto zone = arg.zone;
	auto rule = arg.rule;
	auto reason = arg.location_reason;
	auto confirm = arg.confirm;
	///////kdiy///////
	// uint8_t move_player = (target->to_field_param >> 24) & 0xff;
	// uint8_t playerid = (target->to_field_param >> 16) & 0xff;
	// uint8_t location = (target->to_field_param >> 8) & 0xff;
	// uint8_t positions = (target->to_field_param) & 0xff;
	uint8_t move_player = (target->to_field_param >> 28) & 0xf;
	uint8_t playerid = (target->to_field_param >> 24) & 0xf;
	uint8_t Rloc = (target->to_field_param >> 16) & 0xff;
	uint8_t location = (target->to_field_param >> 8) & 0xff;
	uint8_t positions = (target->to_field_param) & 0xff;
	///////kdiy///////
	switch(arg.step) {
	case 0: {
		returns.set<int32_t>(0, FALSE);
		if((ret == 1) && (!(target->current.reason & REASON_TEMPORARY) || (target->current.reason_effect->owner != core.reason_effect->owner)))
			return TRUE;
		if(location == LOCATION_SZONE && zone == (0x1 << 5) && (target->data.type & TYPE_FIELD) && (target->data.type & (TYPE_SPELL | TYPE_TRAP))) {
			card* pcard = get_field_card(playerid, LOCATION_SZONE, 5);
			if(pcard) {
				if(!is_flag(DUEL_1_FACEUP_FIELD))
					send_to(pcard, nullptr, REASON_RULE, pcard->current.controler, PLAYER_NONE, LOCATION_GRAVE, 0, 0);
				else
					destroy(pcard, nullptr, REASON_RULE, pcard->current.controler);
				adjust_all();
			}
		//////////kdiy////////////
		//} else if(pzone && location == LOCATION_SZONE && (target->data.type & TYPE_PENDULUM) && is_flag(DUEL_PZONE)) {
		} else if(pzone && location == LOCATION_SZONE && !target->is_affected_by_effect(EFFECT_ORICA_SZONE) && (target->data.type & TYPE_PENDULUM) && is_flag(DUEL_PZONE)) {
		//////////kdiy////////////
			uint32_t flag = 0;
			if(is_location_useable(playerid, LOCATION_PZONE, 0) && zone & 1)
				flag |= 0x1u << (get_pzone_index(0, playerid) + 8);
			if(is_location_useable(playerid, LOCATION_PZONE, 1) && zone & 2)
				flag |= 0x1u << (get_pzone_index(1, playerid) + 8);
			if(!flag)
				return TRUE;
			if(move_player != playerid)
				flag = flag << 16;
			flag = ~flag;
			auto message = pduel->new_message(MSG_HINT);
			message->write<uint8_t>(HINT_SELECTMSG);
			message->write<uint8_t>(move_player);
			message->write<uint64_t>(target->data.code);
			emplace_process<Processors::SelectPlace>(move_player, flag, 1);
		} else {
			uint32_t flag;
			///////////kdiy//////////
			//uint32_t lreason = reason ? reason : (target->current.location == LOCATION_MZONE) ? LOCATION_REASON_CONTROL : LOCATION_REASON_TOFIELD;
			uint32_t lreason = reason ? reason : ((target->current.location == LOCATION_MZONE && !target->is_affected_by_effect(EFFECT_SANCT_MZONE)) || (target->current.location == LOCATION_SZONE && target->is_affected_by_effect(EFFECT_ORICA_SZONE))) ? LOCATION_REASON_CONTROL : LOCATION_REASON_TOFIELD;
            uint32_t zone2 = zone;
            uint32_t zone = zone2 & 0xff;
			if(location == LOCATION_SZONE && is_player_affected_by_effect(playerid, EFFECT_SANCT) && Rloc != 0 && Rloc != 0x40 && Rloc != 0x80)
			    zone |= (Rloc << 8);
			//////////kdiy//////////
			int32_t ct = get_useable_count(target, playerid, location, move_player, lreason, zone, &flag);
			if(location == LOCATION_MZONE && (zone & 0x60) && (zone != 0xff) && !rule) {
				if((zone & 0x20) && is_location_useable(playerid, location, 5)) {
					flag = flag & ~(1u << 5);
					++ct;
				}
				if((zone & 0x40) && is_location_useable(playerid, location, 6)) {
					flag = flag & ~(1u << 6);
					++ct;
				}
			}
            ///////////kdiy//////////
			//if(location == LOCATION_SZONE)
				//flag = flag | ~zone;
            ///////////kdiy//////////
			if((ret == 1) && (ct <= 0 || target->is_status(STATUS_FORBIDDEN) || (!(positions & POS_FACEDOWN) && check_unique_onfield(target, playerid, location)))) {
				arg.step = 3;
				send_to(target, core.reason_effect, REASON_RULE, core.reason_player, PLAYER_NONE, LOCATION_GRAVE, 0, 0);
				return FALSE;
			}
			if(ct <= 0 || ~flag == 0)
				return TRUE;
			if(!confirm && (zone & (zone - 1)) == 0) {
				for(uint8_t seq = 0; seq < 8; ++seq) {
					if((1 << seq) & zone) {
                        ///////////kdiy//////////
                        if(Rloc == 0x40)
                            returns.set<int8_t>(1, LOCATION_MZONE);
                         if(Rloc == 0x80)
                            returns.set<int8_t>(1, LOCATION_SZONE);
                        ///////////kdiy//////////
						returns.set<int8_t>(2, seq);
						return FALSE;
					}
				}
			}
			if(!is_flag(DUEL_TRAP_MONSTERS_NOT_USE_ZONE) && (ret == 2)) {
                ///////////kdiy//////////
                returns.set<int8_t>(1, target->previous.location);
                ///////////kdiy//////////
				returns.set<int8_t>(2, target->previous.sequence);
				return FALSE;
			}
			if(move_player == playerid) {
				if(location == LOCATION_SZONE)
				    //////kdiy/////
					//flag = ((flag & 0xff) << 8) | 0xffff00ff;
			        {
					   if(is_player_affected_by_effect(playerid, EFFECT_SANCT) && Rloc != 0x80)
						    flag = (flag & 0xff1f) | 0xffff00e0;
					   else
					        flag = (flag & 0xff00) | 0xffff00ff;
					}
				    //////kdiy/////
				else
				    //////kdiy/////
					//flag = (flag & 0xff) | 0xffffff00;
					{
					   if(is_player_affected_by_effect(playerid, EFFECT_ORICA) && Rloc != 0x40)
					        flag = (flag & 0x1fff) | 0xffffe000;
					   else
					        flag = (flag & 0xff) | 0xffffff00;
				    }
				    //////kdiy/////
			} else {
				if(location == LOCATION_SZONE)
				    //////kdiy/////
					//flag = ((flag & 0xff) << 24) | 0xffffff;
			        {
					   if(is_player_affected_by_effect(playerid, EFFECT_SANCT) && Rloc != 0x80)
					       flag = ((flag & 0xff1f) << 16) | 0xe0ffff;
					   else
					       flag = ((flag & 0xff00) << 16) | 0xffffff;
					}
				    //////kdiy/////
				else
				    //////kdiy/////
					//flag = ((flag & 0xff) << 16) | 0xff00ffff;
			        {
					   if(is_player_affected_by_effect(playerid, EFFECT_ORICA) && Rloc != 0x40)
					       flag = ((flag & 0x1fff) << 16) | 0xe000ffff;
					   else
					       flag = ((flag & 0xff) << 16) | 0xff00ffff;
					}
				    //////kdiy/////
			}	
			flag |= 0xe080e080;			
			auto message = pduel->new_message(MSG_HINT);
			message->write<uint8_t>(HINT_SELECTMSG);
			message->write<uint8_t>(move_player);
			message->write<uint64_t>(target->data.code);
			emplace_process<Processors::SelectPlace>(move_player, flag, 1);
		}
		return FALSE;
	}
	case 1: {
		uint32_t seq = returns.at<int8_t>(2);
		//kdiy///////
		target->temp.location = returns.at<int8_t>(1);
		uint32_t orica = 0;
		uint32_t sanct = 0;
		effect* oeffect = is_player_affected_by_effect(playerid, EFFECT_ORICA);
		effect* seffect = is_player_affected_by_effect(playerid, EFFECT_SANCT);
		if(location == LOCATION_MZONE && target->is_affected_by_effect(EFFECT_ORICA_SZONE))
			orica = 1;
		if(location == LOCATION_SZONE && target->is_affected_by_effect(EFFECT_SANCT_MZONE))
			sanct = 1;
		//kdiy///////
		if(location == LOCATION_SZONE && zone == 0x1 << 5 && (target->data.type & TYPE_FIELD) && (target->data.type & (TYPE_SPELL | TYPE_TRAP)))
			seq = 5;
		if(ret != 1) {
			//kdiy//////
			//if(location != target->current.location) {
			if(location != target->prev_temp.location && (Rloc != 0x40 && Rloc != 0x80)) {
			//kdiy//////
				uint32_t resetflag = 0;
				if(location & LOCATION_ONFIELD)
					resetflag |= RESET_TOFIELD;
				if(target->current.location & LOCATION_ONFIELD)
					resetflag |= RESET_LEAVE;
				effect* peffect = target->is_affected_by_effect(EFFECT_PRE_MONSTER);
				if((location & LOCATION_ONFIELD) && (target->current.location & LOCATION_ONFIELD)
					&& !(peffect && (peffect->value & TYPE_TRAP)) && ret != 2)
					resetflag |= RESET_MSCHANGE;
				target->reset(resetflag, RESET_EVENT);
				target->clear_card_target();
			}
		}
		if(!(target->current.location & LOCATION_ONFIELD))
			target->clear_relate_effect();
		if(ret == 1)
			target->current.reason &= ~REASON_TEMPORARY;
		////kdiy/////
		if(oeffect && orica == 1 && !target->is_affected_by_effect(EFFECT_ORICA_SZONE)) {
			effect* deffect = pduel->new_effect();
			deffect->owner = oeffect->owner;
			deffect->code = EFFECT_ORICA_SZONE;
			deffect->type = EFFECT_TYPE_SINGLE;
			deffect->flag[0] = EFFECT_FLAG_CANNOT_DISABLE | EFFECT_FLAG_IGNORE_IMMUNE | EFFECT_FLAG_UNCOPYABLE;
			deffect->reset_flag = RESET_EVENT+0x1fe0000+RESET_CONTROL-RESET_TURN_SET;
			target->add_effect(deffect);
            target->reset(EFFECT_SANCT_MZONE, RESET_CODE);
		} else if(seffect && sanct == 1 && !target->is_affected_by_effect(EFFECT_SANCT_MZONE)) {
			effect* deffect = pduel->new_effect();
			deffect->owner = seffect->owner;
			deffect->code = EFFECT_SANCT_MZONE;
			deffect->type = EFFECT_TYPE_SINGLE;
			deffect->flag[0] = EFFECT_FLAG_CANNOT_DISABLE | EFFECT_FLAG_IGNORE_IMMUNE | EFFECT_FLAG_UNCOPYABLE;
			deffect->reset_flag = RESET_EVENT+0x1fe0000+RESET_CONTROL-RESET_TURN_SET;
			target->add_effect(deffect);
            target->reset(EFFECT_ORICA_SZONE, RESET_CODE);
		}
		// if(target->temp.location != LOCATION_SZONE && target->is_affected_by_effect(EFFECT_ORICA_SZONE))
		//     target->reset(EFFECT_ORICA_SZONE, RESET_CODE);
		// if(target->temp.location != LOCATION_MZONE && target->is_affected_by_effect(EFFECT_SANCT_MZONE))
		//     target->reset(EFFECT_SANCT_MZONE, RESET_CODE);
		//if((ret == 0 && location != target->current.location)
		if((ret == 0 && location != target->prev_temp.location && (Rloc != 0x40 && Rloc != 0x80))
		////kdiy/////
			|| ret == 1) {
			target->set_status(STATUS_SUMMON_TURN, FALSE);
			target->set_status(STATUS_FLIP_SUMMON_TURN, FALSE);
			target->set_status(STATUS_SPSUMMON_TURN, FALSE);
			target->set_status(STATUS_SET_TURN, FALSE);
			target->set_status(STATUS_FORM_CHANGED, FALSE);
		}
		target->temp.sequence = seq;
		if(location != LOCATION_MZONE) {
			returns.set<int32_t>(0, positions);
			return FALSE;
		}
		if((target->data.type & TYPE_LINK) && (target->data.type & TYPE_MONSTER)) {
			returns.set<int32_t>(0, POS_FACEUP_ATTACK);
			return FALSE;
		}
		emplace_process<Processors::SelectPosition>(move_player, target->data.code, positions);
		return FALSE;
	}
	case 2: {			
		if(core.global_flag & GLOBALFLAG_DECK_REVERSE_CHECK) {
			if(target->current.location == LOCATION_DECK) {
				uint32_t curp = target->current.controler;
				uint32_t curs = target->current.sequence;
				if(curs > 0 && (curs == player[curp].list_main.size() - 1)) {
					card* ptop = player[curp].list_main[curs - 1];
					if(core.deck_reversed || (ptop->current.position == POS_FACEUP_DEFENSE)) {
						auto message = pduel->new_message(MSG_DECK_TOP);
						message->write<uint8_t>(curp);
						message->write<uint32_t>(1);
						message->write<uint32_t>(ptop->data.code);
						message->write<uint32_t>(ptop->current.position);
					}
				}
			}
		}
		auto message = pduel->new_message(MSG_MOVE);
		message->write<uint32_t>(target->data.code);
		message->write(target->get_info_location());
		if(target->overlay_target)
			target->overlay_target->xyz_remove(target);
		// call move_card()
		//kdiy///////
        bool temp_pzone = target->current.pzone;
		int8_t location2 = target->temp.location;
		target->temp.location = location;
		if(location2 == LOCATION_MZONE || location2 == LOCATION_SZONE)
		move_card(playerid, target, location2, target->temp.sequence, pzone);
		else
		//kdiy///////
		move_card(playerid, target, location, target->temp.sequence, pzone);
		target->current.position = returns.at<int32_t>(0);
		target->set_status(STATUS_LEAVE_CONFIRMED, FALSE);
		message->write(target->get_info_location());
		message->write<uint32_t>(target->current.reason);
		////kdiy///////
		message->write<uint8_t>(target->current.reason_player);
        message->write<bool>(pzone && !temp_pzone);
        message->write<bool>(true);
        message->write<bool>((location == LOCATION_SZONE && !target->is_affected_by_effect(EFFECT_ORICA_SZONE)) || target->is_affected_by_effect(EFFECT_SANCT_MZONE));
		target->temp.location = 0;
		target->prev_temp.location = 0;
		//if((target->current.location != LOCATION_MZONE)) {
		if(!((target->current.location == LOCATION_MZONE && !target->is_affected_by_effect(EFFECT_SANCT_MZONE)) || (target->current.location == LOCATION_SZONE && target->is_affected_by_effect(EFFECT_ORICA_SZONE))) && (Rloc != 0x40 && Rloc != 0x80)) {
		////kdiy///////
			if(target->equiping_cards.size()) {
				destroy(target->equiping_cards, nullptr, REASON_LOST_TARGET + REASON_RULE, PLAYER_NONE);
				for(auto csit = target->equiping_cards.begin(); csit != target->equiping_cards.end();) {
					auto rm = csit++;
					(*rm)->unequip();
				}
			}
			if(target->xyz_materials.size()) {
				send_to(card_set{ target->xyz_materials.begin(), target->xyz_materials.end() }, nullptr, REASON_LOST_TARGET + REASON_RULE, PLAYER_NONE, PLAYER_NONE, LOCATION_GRAVE, 0, POS_FACEUP);
			}
		}
		////kdiy///////
		//if((target->previous.location == LOCATION_SZONE) && target->equiping_target)
			//target->unequip();
		//if(target->current.location == LOCATION_MZONE) {
		if((target->previous.location == LOCATION_SZONE || target->previous.location == LOCATION_MZONE) && target->equiping_target)
			target->unequip();
		if((target->current.location == LOCATION_MZONE && !target->is_affected_by_effect(EFFECT_SANCT_MZONE)) || (target->current.location == LOCATION_SZONE && target->is_affected_by_effect(EFFECT_ORICA_SZONE)) && (Rloc != 0x40 && Rloc != 0x80)) {
		////kdiy///////
			effect_set eset;
			filter_player_effect(0, EFFECT_MUST_USE_MZONE, &eset, false);
			filter_player_effect(1, EFFECT_MUST_USE_MZONE, &eset, false);
			target->filter_effect(EFFECT_MUST_USE_MZONE, &eset);
			////kdiy///////
			//uint32_t lreason = reason ? reason : (target->current.location == LOCATION_MZONE) ? LOCATION_REASON_CONTROL : LOCATION_REASON_TOFIELD;
			uint32_t lreason = reason ? reason : ((target->current.location == LOCATION_MZONE && !target->is_affected_by_effect(EFFECT_SANCT_MZONE)) || (target->current.location == LOCATION_SZONE && target->is_affected_by_effect(EFFECT_ORICA_SZONE))) ? LOCATION_REASON_CONTROL : LOCATION_REASON_TOFIELD;
			////kdiy///////
			for(const auto& peff : eset) {
				if(peff->is_flag(EFFECT_FLAG_COUNT_LIMIT) && peff->count_limit == 0)
					continue;
				if(peff->operation) {
					pduel->lua->add_param<LuaParam::EFFECT>(peff, true);
					pduel->lua->add_param<LuaParam::INT>(target->current.controler);
					pduel->lua->add_param<LuaParam::INT>(move_player);
					pduel->lua->add_param<LuaParam::INT>(lreason);
					if(!pduel->lua->check_condition(peff->operation, 4))
						continue;
				}
				uint32_t value = 0x1f;
				if(peff->is_flag(EFFECT_FLAG_PLAYER_TARGET)) {
					pduel->lua->add_param<LuaParam::INT>(target->current.controler);
					pduel->lua->add_param<LuaParam::INT>(move_player);
					pduel->lua->add_param<LuaParam::INT>(lreason);
					value = peff->get_value(3);
				} else {
					pduel->lua->add_param<LuaParam::INT>(target->current.controler);
					pduel->lua->add_param<LuaParam::INT>(move_player);
					pduel->lua->add_param<LuaParam::INT>(lreason);
					value = peff->get_value(target, 3);
				}
				if(peff->get_handler_player() != target->current.controler)
					value = value >> 16;
				if(value & (0x1 << target->current.sequence)) {
					peff->dec_count();
				}
			}
			effect* teffect;
			if((teffect = target->is_affected_by_effect(EFFECT_PRE_MONSTER)) != nullptr) {
				uint32_t type = teffect->value;
				if(type & TYPE_TRAP)
					type |= TYPE_TRAPMONSTER | target->data.type;
				target->reset(EFFECT_PRE_MONSTER, RESET_CODE);
				effect* peffect = pduel->new_effect();
				peffect->owner = target;
				peffect->type = EFFECT_TYPE_SINGLE;
				peffect->code = EFFECT_CHANGE_TYPE;
				peffect->flag[0] = EFFECT_FLAG_CANNOT_DISABLE;
				peffect->reset_flag = RESET_EVENT + 0x1fc0000;
				peffect->value = TYPE_MONSTER | type;
				target->add_effect(peffect);
				if(!is_flag(DUEL_TRAP_MONSTERS_NOT_USE_ZONE) &&(type & TYPE_TRAPMONSTER)) {
					peffect = pduel->new_effect();
					peffect->owner = target;
					peffect->type = EFFECT_TYPE_FIELD;
					////kdiy///////
					if(target->current.location == LOCATION_SZONE)
					peffect->range = LOCATION_SZONE;
					else
					////kdiy///////
					peffect->range = LOCATION_MZONE;
					////kdiy///////
					if(target->previous.location == LOCATION_MZONE)
					peffect->code = EFFECT_USE_EXTRA_MZONE;
					else
					////kdiy///////
					peffect->code = EFFECT_USE_EXTRA_SZONE;
					peffect->flag[0] = EFFECT_FLAG_CANNOT_DISABLE;
					peffect->reset_flag = RESET_EVENT + 0x1fe0000;
					peffect->value = 1 + (0x10000 << target->previous.sequence);
					target->add_effect(peffect);
				}
			}
		}
		if(enable || ((ret == 1) && target->is_position(POS_FACEUP)))
			target->enable_field_effect(true);
		////////////kdiy///////
		//if(ret == 1 && target->current.location == LOCATION_MZONE && !(target->data.type & TYPE_MONSTER))
		if(ret == 1 && ((target->current.location == LOCATION_MZONE && !target->is_affected_by_effect(EFFECT_SANCT_MZONE)) || (target->current.location == LOCATION_SZONE && target->is_affected_by_effect(EFFECT_ORICA_SZONE))) && !(target->data.type & TYPE_MONSTER))
		////////////kdiy///////
			send_to(target, nullptr, REASON_RULE, PLAYER_NONE, PLAYER_NONE, LOCATION_GRAVE, 0, 0);
		else {
			if(target->previous.location == LOCATION_GRAVE) {
				raise_single_event(target, nullptr, EVENT_LEAVE_GRAVE, target->current.reason_effect, target->current.reason, move_player, 0, 0);
				raise_event(target, EVENT_LEAVE_GRAVE, target->current.reason_effect, target->current.reason, move_player, 0, 0);
			}
			raise_single_event(target, nullptr, EVENT_MOVE, target->current.reason_effect, target->current.reason, target->current.reason_player, 0, 0);
			raise_event(target, EVENT_MOVE, target->current.reason_effect, target->current.reason, target->current.reason_player, 0, 0);
			process_single_event();
			process_instant_event();
		}
		adjust_disable_check_list();			
		return FALSE;
	}
	case 3: {
		returns.set<int32_t>(0, TRUE);
		return TRUE;
	}
	case 4: {
		returns.set<int32_t>(0, FALSE);
		return TRUE;
	}
	}
	return TRUE;
}
bool field::process(Processors::ChangePos& arg) {
	auto targets = arg.targets;
	auto reason_effect = arg.reason_effect;
	auto reason_player = arg.reason_player;
	auto enable = arg.enable;
	switch(arg.step) {
	case 0: {
		for(auto cit = targets->container.begin(); cit != targets->container.end();) {
			card* pcard = *cit++;
			uint8_t npos = pcard->position_param & 0xff;
			uint8_t opos = pcard->current.position;
			if((pcard->current.location != LOCATION_MZONE && pcard->current.location != LOCATION_SZONE)
			   || ((pcard->data.type & TYPE_LINK) && (pcard->data.type & TYPE_MONSTER))
			   || pcard->get_status(STATUS_SUMMONING | STATUS_SPSUMMON_STEP)
			   || (reason_effect && !pcard->is_affect_by_effect(reason_effect)) || npos == opos
			   || (!(pcard->data.type & TYPE_TOKEN) && (opos & POS_FACEUP) && (npos & POS_FACEDOWN) && !pcard->is_capable_turn_set(reason_player))) {
				targets->container.erase(pcard);
				continue;
			}
			//For cards that cannot be changed to an specific position via effects
			if(!reason_effect)
				continue;
			effect_set eset;
			pcard->filter_effect(EFFECT_CANNOT_CHANGE_POS_E, &eset);
			if(eset.empty())
				continue;
			uint8_t disallowpos = 0;
			for(const auto& eff : eset) {
				auto val = eff->get_value(reason_effect);
				disallowpos |= val ? val : POS_FACEUP | POS_FACEDOWN;
			}
			if(npos & disallowpos) {
				targets->container.erase(pcard);
				continue;
			}
		}
		arg.to_grave_set.clear();
		return FALSE;
	}
	case 1: {
		uint8_t playerid = reason_player;
		if(arg.oppo_selection)
			playerid = 1 - reason_player;
		card_set ssets;
		for(auto& pcard : targets->container) {
			uint8_t npos = pcard->position_param & 0xff;
			uint8_t opos = pcard->current.position;
			if((opos & POS_FACEUP) && (npos & POS_FACEDOWN)) {
				if(pcard->get_type() & TYPE_TRAPMONSTER) {
					if(pcard->current.controler == playerid)
						ssets.insert(pcard);
				}
			}
		}
		if(ssets.size()) {
			return_cards.clear();
			refresh_location_info_instant();
			int32_t fcount = get_useable_count(nullptr, playerid, LOCATION_SZONE, playerid, 0);
			if(fcount <= 0) {
				for(auto& pcard : ssets) {
					arg.to_grave_set.insert(pcard);
					targets->container.erase(pcard);
				}
				arg.step = 2;
			} else if((int32_t)ssets.size() > fcount) {
				core.select_cards.clear();
				for(auto& pcard : ssets)
					core.select_cards.push_back(pcard);
				uint32_t ct = (uint32_t)ssets.size() - fcount;
				auto message = pduel->new_message(MSG_HINT);
				message->write<uint8_t>(HINT_SELECTMSG);
				message->write<uint8_t>(playerid);
				message->write<uint64_t>(502);
				emplace_process<Processors::SelectCard>(playerid, false, ct, ct);
			}
		} else
			arg.step = 2;
		return FALSE;
	}
	case 2: {
		for(auto& pcard : return_cards.list) {
			arg.to_grave_set.insert(pcard);
			targets->container.erase(pcard);
		}
		return FALSE;
	}
	case 3: {
		if(!arg.oppo_selection) {
			arg.oppo_selection = true;
			arg.step = 0;
		}
		return FALSE;
	}
	case 4: {
		card_set equipings;
		card_set flips;
		card_set ssets;
		card_set pos_changed;
		card_vector cv(targets->container.begin(), targets->container.end());
		if(cv.size() > 1)
			std::sort(cv.begin(), cv.end(), card::card_operation_sort);
		for(auto& pcard : cv) {
			uint8_t npos = pcard->position_param & 0xff;
			uint8_t opos = pcard->current.position;
			uint8_t flag = pcard->position_param >> 16;
			if((pcard->data.type & TYPE_TOKEN) && (npos & POS_FACEDOWN))
				npos = POS_FACEUP_DEFENSE;
			pcard->previous.position = opos;
			pcard->current.position = npos;
			if((npos & POS_DEFENSE) && !pcard->is_affected_by_effect(EFFECT_DEFENSE_ATTACK))
				pcard->set_status(STATUS_ATTACK_CANCELED, TRUE);
			pcard->set_status(STATUS_JUST_POS, TRUE);
			auto message = pduel->new_message(MSG_POS_CHANGE);
			message->write<uint32_t>(pcard->data.code);
			message->write<uint8_t>(pcard->current.controler);
			message->write<uint8_t>(pcard->current.location);
			message->write<uint8_t>(pcard->current.sequence);
			message->write<uint8_t>(pcard->previous.position);
			message->write<uint8_t>(pcard->current.position);
			core.hint_timing[pcard->current.controler] |= TIMING_POS_CHANGE;
			if((opos & POS_FACEDOWN) && (npos & POS_FACEUP)) {
				pcard->fieldid = infos.field_id++;
				if(check_unique_onfield(pcard, pcard->current.controler, pcard->current.location))
					pcard->unique_fieldid = UINT_MAX;
				////////kdiy///////
				//if(pcard->current.location == LOCATION_MZONE) {
				if((pcard->current.location == LOCATION_MZONE && !pcard->is_affected_by_effect(EFFECT_SANCT_MZONE)) || (pcard->current.location == LOCATION_SZONE && pcard->is_affected_by_effect(EFFECT_ORICA_SZONE))) {
				////////kdiy///////
					raise_single_event(pcard, nullptr, EVENT_FLIP, reason_effect, 0, reason_player, 0, flag);
					flips.insert(pcard);
				}
				if(enable) {
					////////kdiy///////	
					//if(!reason_effect || !(reason_effect->type & 0x7f0) || pcard->current.location != LOCATION_MZONE)
					if(!reason_effect || !(reason_effect->type & 0x7f0) || !((pcard->current.location == LOCATION_MZONE && !pcard->is_affected_by_effect(EFFECT_SANCT_MZONE)) || (pcard->current.location == LOCATION_SZONE && pcard->is_affected_by_effect(EFFECT_ORICA_SZONE))))
					////////kdiy///////	
						pcard->enable_field_effect(true);
					else
						core.delayed_enable_set.insert(pcard);
				} else
					pcard->refresh_disable_status();
			}
			////////kdiy///////
			//if(pcard->current.location == LOCATION_MZONE) {
			if((pcard->current.location == LOCATION_MZONE && !pcard->is_affected_by_effect(EFFECT_SANCT_MZONE)) || (pcard->current.location == LOCATION_SZONE && pcard->is_affected_by_effect(EFFECT_ORICA_SZONE))) {
			////////kdiy///////
				raise_single_event(pcard, nullptr, EVENT_CHANGE_POS, reason_effect, 0, reason_player, 0, 0);
				pos_changed.insert(pcard);
			}
			bool trapmonster = false;
			if((opos & POS_FACEUP) && (npos & POS_FACEDOWN)) {
				if(pcard->get_type() & TYPE_TRAPMONSTER)
					trapmonster = true;
				if(pcard->status & (STATUS_SUMMON_DISABLED | STATUS_ACTIVATE_DISABLED))
					pcard->set_status(STATUS_SUMMON_DISABLED | STATUS_ACTIVATE_DISABLED, FALSE);
				pcard->reset(RESET_TURN_SET, RESET_EVENT);
				pcard->clear_card_target();
				pcard->set_status(STATUS_SET_TURN, TRUE);
				pcard->enable_field_effect(false);
				pcard->previous.location = 0;
				pcard->summon.type &= 0xdf00ffff;
				if((pcard->summon.type & SUMMON_TYPE_PENDULUM) == SUMMON_TYPE_PENDULUM)
					pcard->summon.type &= 0xf000ffff;
				pcard->spsummon_counter[0] = pcard->spsummon_counter[1] = 0;
				pcard->spsummon_counter_rst[0] = pcard->spsummon_counter_rst[1] = 0;
			}
			if((npos & POS_FACEDOWN) && pcard->equiping_cards.size()) {
				for(auto csit = pcard->equiping_cards.begin(); csit != pcard->equiping_cards.end();) {
					auto erm = csit++;
					equipings.insert(*erm);
					(*erm)->unequip();
				}
			}
			if((npos & POS_FACEDOWN) && pcard->equiping_target)
				pcard->unequip();
			if(trapmonster) {
				refresh_location_info_instant();
				///////kdiy///////
				pcard->prev_temp.location = LOCATION_MZONE;
				effect* seffect = is_player_affected_by_effect(pcard->current.controler,EFFECT_SANCT);	
				if(seffect && !pcard->is_affected_by_effect(EFFECT_SANCT_MZONE)) {
					effect* deffect = pduel->new_effect();
					deffect->owner = seffect->owner;
					deffect->code = EFFECT_SANCT_MZONE;
					deffect->type = EFFECT_TYPE_SINGLE;
					deffect->flag[0] = EFFECT_FLAG_CANNOT_DISABLE | EFFECT_FLAG_IGNORE_IMMUNE | EFFECT_FLAG_UNCOPYABLE;
					deffect->reset_flag = RESET_EVENT+0x1fe0000+RESET_CONTROL-RESET_TURN_SET;
					pcard->add_effect(deffect);
                    pcard->reset(EFFECT_ORICA_SZONE, RESET_CODE);
				}
				///////kdiy///////
				move_to_field(pcard, pcard->current.controler, pcard->current.controler, LOCATION_SZONE, POS_FACEDOWN, FALSE, 2);
				raise_single_event(pcard, nullptr, EVENT_SSET, reason_effect, 0, reason_player, 0, 0);
				ssets.insert(pcard);
			}
		}
		adjust_instant();
		process_single_event();
		if(flips.size())
			raise_event(std::move(flips), EVENT_FLIP, reason_effect, 0, reason_player, 0, 0);
		if(ssets.size())
			raise_event(std::move(ssets), EVENT_SSET, reason_effect, 0, reason_player, 0, 0);
		if(pos_changed.size())
			raise_event(std::move(pos_changed), EVENT_CHANGE_POS, reason_effect, 0, reason_player, 0, 0);
		process_instant_event();
		if(equipings.size())
			destroy(std::move(equipings), nullptr, REASON_LOST_TARGET + REASON_RULE, PLAYER_NONE);
		auto& to_grave_set = arg.to_grave_set;
		if(to_grave_set.size()) {
			send_to(std::move(to_grave_set), nullptr, REASON_RULE, PLAYER_NONE, PLAYER_NONE, LOCATION_GRAVE, 0, POS_FACEUP);
		}
		return FALSE;
	}
	case 5: {
		core.operated_set.clear();
		core.operated_set = targets->container;
		returns.set<int32_t>(0, static_cast<int32_t>(targets->container.size()));
		return TRUE;
	}
	}
	return TRUE;
}
bool field::process(Processors::OperationReplace& arg) {
	auto replace_effect = arg.replace_effect;
	auto targets = arg.targets;
	auto target = arg.target;
	auto is_destroy = arg.is_destroy;
	switch(arg.step) {
	case 0: {
		if(returns.at<int32_t>(0))
			return TRUE;
		if(!replace_effect->target)
			return TRUE;
		tevent e;
		e.event_cards = targets;
		e.event_player = replace_effect->get_handler_player();
		e.event_value = 0;
		e.reason = target->current.reason;
		e.reason_effect = target->current.reason_effect;
		e.reason_player = target->current.reason_player;
		if(!replace_effect->is_activateable(replace_effect->get_handler_player(), e))
			return TRUE;
		auto& newchain = core.continuous_chain.emplace_back();
		newchain.chain_id = 0;
		newchain.chain_count = 0;
		newchain.triggering_effect = replace_effect;
		newchain.triggering_player = e.event_player;
		newchain.evt = e;
		newchain.target_cards = nullptr;
		newchain.target_player = PLAYER_NONE;
		newchain.target_param = 0;
		newchain.disable_player = PLAYER_NONE;
		newchain.disable_reason = nullptr;
		newchain.flag = 0;
		core.solving_event.push_front(e);
		core.sub_solving_event.push_back(std::move(e));
		emplace_process<Processors::ExecuteTarget>(replace_effect, replace_effect->get_handler_player());
		return FALSE;
	}
	case 1: {
		if (returns.at<int32_t>(0)) {
			if(!(target->current.reason_effect && target->current.reason_effect->is_self_destroy_related())) {
				targets->container.erase(target);
				target->current.reason = target->temp.reason;
				target->current.reason_effect = target->temp.reason_effect;
				target->current.reason_player = target->temp.reason_player;
				if(is_destroy)
					core.destroy_canceled.insert(target);
			}
			replace_effect->dec_count(replace_effect->get_handler_player());
		} else
			arg.step = 2;
		return FALSE;
	}
	case 2: {
		if(!replace_effect->operation)
			return FALSE;
		core.sub_solving_event.push_back(*core.solving_event.begin());
		emplace_process<Processors::ExecuteOperation>(replace_effect, replace_effect->get_handler_player());
		return FALSE;
	}
	case 3: {
		core.continuous_chain.pop_back();
		core.solving_event.pop_front();
		return TRUE;
	}
	case 5: {
		if(targets->container.size() == 0)
			return TRUE;
		tevent e;
		e.event_cards = targets;
		e.event_player = replace_effect->get_handler_player();
		e.event_value = 0;
		card* pc = *targets->container.begin();
		e.reason = pc->current.reason;
		e.reason_effect = pc->current.reason_effect;
		e.reason_player = pc->current.reason_player;
		if(!replace_effect->is_activateable(replace_effect->get_handler_player(), e) || !replace_effect->value)
			return TRUE;
		auto& newchain = core.continuous_chain.emplace_back();
		newchain.chain_id = 0;
		newchain.chain_count = 0;
		newchain.triggering_effect = replace_effect;
		newchain.triggering_player = e.event_player;
		newchain.evt = e;
		newchain.target_cards = nullptr;
		newchain.target_player = PLAYER_NONE;
		newchain.target_param = 0;
		newchain.disable_player = PLAYER_NONE;
		newchain.disable_reason = nullptr;
		newchain.flag = 0;
		core.solving_event.push_front(e);
		core.sub_solving_event.push_back(std::move(e));
		emplace_process<Processors::ExecuteTarget>(replace_effect, replace_effect->get_handler_player());
		return FALSE;
	}
	case 6: {
		if(returns.at<int32_t>(0)) {
			for(auto cit = targets->container.begin(); cit != targets->container.end();) {
				auto rm = cit++;
				if(replace_effect->get_value(*rm) && !((*rm)->current.reason_effect && (*rm)->current.reason_effect->is_self_destroy_related())) {
					(*rm)->current.reason = (*rm)->temp.reason;
					(*rm)->current.reason_effect = (*rm)->temp.reason_effect;
					(*rm)->current.reason_player = (*rm)->temp.reason_player;
					if(is_destroy)
						core.destroy_canceled.insert(*rm);
					targets->container.erase(rm);
				}
			}
			replace_effect->dec_count(replace_effect->get_handler_player());
		} else
			arg.step = 7;
		return FALSE;
	}
	case 7: {
		if(!replace_effect->operation)
			return FALSE;
		core.sub_solving_event.push_back(*core.solving_event.begin());
		emplace_process<Processors::ExecuteOperation>(replace_effect, replace_effect->get_handler_player());
		return FALSE;
	}
	case 8: {
		core.continuous_chain.pop_back();
		core.solving_event.pop_front();
		return TRUE;
	}
	case 10: {
		if(returns.at<int32_t>(0))
			return TRUE;
		if(!replace_effect->target)
			return TRUE;
		tevent e;
		e.event_cards = targets;
		e.event_player = replace_effect->get_handler_player();
		e.event_value = 0;
		e.reason = target->current.reason;
		e.reason_effect = target->current.reason_effect;
		e.reason_player = target->current.reason_player;
		if(!replace_effect->is_activateable(replace_effect->get_handler_player(), e))
			return TRUE;
		auto& newchain = core.continuous_chain.emplace_back();
		newchain.chain_id = 0;
		newchain.chain_count = 0;
		newchain.triggering_effect = replace_effect;
		newchain.triggering_player = e.event_player;
		newchain.evt = e;
		newchain.target_cards = nullptr;
		newchain.target_player = PLAYER_NONE;
		newchain.target_param = 0;
		newchain.disable_player = PLAYER_NONE;
		newchain.disable_reason = nullptr;
		newchain.flag = 0;
		core.sub_solving_event.push_back(std::move(e));
		emplace_process<Processors::ExecuteTarget>(replace_effect, replace_effect->get_handler_player());
		return FALSE;
	}
	case 11: {
		if(returns.at<int32_t>(0)) {
			targets->container.erase(target);
			target->current.reason = target->temp.reason;
			target->current.reason_effect = target->temp.reason_effect;
			target->current.reason_player = target->temp.reason_player;
			if(is_destroy)
				core.destroy_canceled.insert(target);
			replace_effect->dec_count(replace_effect->get_handler_player());
			core.desrep_chain.push_back(core.continuous_chain.front());
		}
		core.continuous_chain.pop_front();
		return TRUE;
	}
	case 12: {
		if(targets->container.size() == 0)
			return TRUE;
		tevent e;
		e.event_cards = targets;
		e.event_player = replace_effect->get_handler_player();
		e.event_value = 0;
		card* pc = *targets->container.begin();
		e.reason = pc->current.reason;
		e.reason_effect = pc->current.reason_effect;
		e.reason_player = pc->current.reason_player;
		if(!replace_effect->is_activateable(replace_effect->get_handler_player(), e) || !replace_effect->value)
			return TRUE;
		auto& newchain = core.continuous_chain.emplace_back();
		newchain.chain_id = 0;
		newchain.chain_count = 0;
		newchain.triggering_effect = replace_effect;
		newchain.triggering_player = e.event_player;
		newchain.evt = e;
		newchain.target_cards = nullptr;
		newchain.target_player = PLAYER_NONE;
		newchain.target_param = 0;
		newchain.disable_player = PLAYER_NONE;
		newchain.disable_reason = nullptr;
		newchain.flag = 0;
		core.sub_solving_event.push_back(std::move(e));
		emplace_process<Processors::ExecuteTarget>(replace_effect, replace_effect->get_handler_player());
		return FALSE;
	}
	case 13: {
		if(returns.at<int32_t>(0)) {
			for(auto cit = targets->container.begin(); cit != targets->container.end();) {
				auto rm = cit++;
				if(replace_effect->get_value(*rm)) {
					(*rm)->current.reason = (*rm)->temp.reason;
					(*rm)->current.reason_effect = (*rm)->temp.reason_effect;
					(*rm)->current.reason_player = (*rm)->temp.reason_player;
					if(is_destroy)
						core.destroy_canceled.insert(*rm);
					targets->container.erase(rm);
				}
			}
			replace_effect->dec_count(replace_effect->get_handler_player());
			core.desrep_chain.push_back(core.continuous_chain.front());
		}
		core.continuous_chain.pop_front();
		return TRUE;
	}
	case 15: {
		if(core.desrep_chain.size() == 0)
			return TRUE;
		core.continuous_chain.splice(core.continuous_chain.end(), core.desrep_chain, core.desrep_chain.begin());
		effect* reffect = core.continuous_chain.back().triggering_effect;
		if(!reffect->operation)
			return FALSE;
		core.sub_solving_event.push_back(core.continuous_chain.back().evt);
		emplace_process<Processors::ExecuteOperation>(reffect, reffect->get_handler_player());
		return FALSE;
	}
	case 16: {
		core.continuous_chain.pop_back();
		arg.step = 14;
		return FALSE;
	}
	}
	return TRUE;
}
bool field::process(Processors::ActivateEffect& arg) {
	auto peffect = arg.peffect;
	switch(arg.step) {
	case 0: {
		card* phandler = peffect->get_handler();
		int32_t playerid = phandler->current.controler;
		nil_event.event_code = EVENT_FREE_CHAIN;
		if(!peffect->is_activateable(playerid, nil_event))
			return TRUE;
		auto& newchain = core.new_chains.emplace_back();
		newchain.flag = 0;
		newchain.chain_id = infos.field_id++;
		newchain.evt.event_code = peffect->code;
		newchain.evt.event_player = PLAYER_NONE;
		newchain.evt.event_value = 0;
		newchain.evt.event_cards = nullptr;
		newchain.evt.reason = 0;
		newchain.evt.reason_effect = nullptr;
		newchain.evt.reason_player = PLAYER_NONE;
		newchain.triggering_effect = peffect;
		newchain.set_triggering_state(phandler);
		newchain.triggering_player = playerid;
		phandler->set_status(STATUS_CHAINING, TRUE);
		peffect->dec_count(playerid);
		emplace_process<Processors::AddChain>();
		emplace_process<Processors::QuickEffect>(false, 1 - playerid);
		infos.priorities[0] = 0;
		infos.priorities[1] = 0;
		return FALSE;
	}
	case 1: {
		for(auto& ch_lim : core.chain_limit)
			ensure_luaL_stack(luaL_unref, pduel->lua->lua_state, LUA_REGISTRYINDEX, ch_lim.function);
		core.chain_limit.clear();
		for(auto& ch : core.current_chain)
			ch.triggering_effect->get_handler()->set_status(STATUS_CHAINING, FALSE);
		emplace_process<Processors::SolveChain>(false, false, false);
		return TRUE;
	}
	}
	return TRUE;
}
bool field::process(Processors::SelectRelease& arg) {
	auto playerid = arg.playerid;
	auto cancelable = arg.cancelable;
	auto min = arg.min;
	auto max = arg.max;
	auto check_field = arg.check_field;
	auto to_check = arg.to_check;
	auto toplayer = arg.toplayer;
	auto zone = arg.zone;
	switch(arg.step) {
	case 0: {
		if(check_field) {
			int32_t ct = 0;
			zone &= (0x1f & get_forced_zones(to_check, toplayer, LOCATION_MZONE, playerid, LOCATION_REASON_TOFIELD));
			ct = get_useable_count(to_check, toplayer, LOCATION_MZONE, playerid, LOCATION_REASON_TOFIELD, zone);
			if(ct < min) {
				arg.must_choose_one = std::make_unique<card_set>();
				for(auto& pcard : core.release_cards) {
					//////////kdiy/////////
					//if((pcard->current.location == LOCATION_MZONE && pcard->current.controler == toplayer && ((zone >> pcard->current.sequence) & 1)))
					if((((pcard->current.location == LOCATION_MZONE && !pcard->is_affected_by_effect(EFFECT_SANCT_MZONE)) || (pcard->current.location == LOCATION_SZONE && pcard->is_affected_by_effect(EFFECT_ORICA_SZONE))) && pcard->current.controler == toplayer && ((zone >> pcard->current.sequence) & 1)))
					//////////kdiy/////////
						arg.must_choose_one->insert(pcard);
				}
			}
		}
		core.operated_set.clear();
		return_cards.clear();
		/*if all the available cards are equal to the minimum, then the selection will always use all the forced cards without needing to check*/
		bool allminimum = core.release_cards_ex_oneof.size() <= 1 && ((int32_t)(core.release_cards_ex.size() + core.release_cards.size() + core.release_cards_ex_oneof.size()) == min);
		/*if only must use cards are available, then the selection will always be correct*/
		bool allmust = ((int32_t)core.release_cards_ex.size() >= max) || (core.release_cards.size() + core.release_cards_ex_oneof.size()) == 0;
		/*only self cards available, no need for special check*/
		bool onlyself = (core.release_cards_ex.size() + core.release_cards_ex_oneof.size()) == 0;
		if((!allminimum && !allmust && !onlyself) || arg.must_choose_one) {
			arg.step = 1;
			return FALSE;
		}
		core.select_cards.clear();
		core.select_cards.insert(core.select_cards.begin(), core.release_cards_ex.begin(), core.release_cards_ex.end());
		if((int32_t)core.release_cards_ex.size() < max) {
			core.select_cards.insert(core.select_cards.begin(), core.release_cards.begin(), core.release_cards.end());
			core.select_cards.insert(core.select_cards.begin(), core.release_cards_ex_oneof.begin(), core.release_cards_ex_oneof.end());
		}
		if(allmust)
			min = static_cast<uint16_t>(core.release_cards_ex.size());
		auto message = pduel->new_message(MSG_HINT);
		message->write<uint8_t>(HINT_SELECTMSG);
		message->write<uint8_t>(playerid);
		message->write<uint64_t>(500);
		emplace_process<Processors::SelectCard>(playerid, cancelable, min, max);
		return FALSE;
	}
	case 1: {
		if(return_cards.canceled)
			return TRUE;
		int32_t count = static_cast<int32_t>(return_cards.list.size());
		core.operated_set.insert(return_cards.list.begin(), return_cards.list.end());
		if((min-count) > 0) {
			/*something wrong happened when selecting*/
			return_cards.clear();
			return_cards.canceled = true;
		} else {
			if(core.release_cards_ex_oneof.size()) {
				effect* peffect = (*core.release_cards_ex_oneof.begin())->is_affected_by_effect(EFFECT_EXTRA_RELEASE_NONSUM);
				core.dec_count_reserve.push_back(peffect);
			}
		}
		return TRUE;
	}
	case 2: {
		core.unselect_cards.assign(core.operated_set.begin(), core.operated_set.end());

		auto finishable = (int32_t)core.operated_set.size() >= min;
		auto must_chosen = !arg.must_choose_one;

		auto& must_choose = arg.must_choose_one;
		uint32_t to_select = 0;
		for(auto& pcard : core.release_cards_ex) {
			if(core.operated_set.find(pcard) == core.operated_set.end()) {
				finishable = FALSE;
				++to_select;
			}
		}
		if(!must_chosen) {
			for(auto& pcard : core.operated_set) {
				if(must_choose->find(pcard) != must_choose->end()) {
					must_chosen = TRUE;
					break;
				}
			}
		}

		int32_t curmax = static_cast<int32_t>(max - core.operated_set.size());
		if(!must_chosen) {
			--curmax;
		}
		curmax -= to_select;

		finishable = finishable && must_chosen;

		card_vector diff;
		diff.insert(diff.begin(), core.release_cards_ex.begin(), core.release_cards_ex.end());
		if(curmax <= 0) {
			if(!must_chosen) {
				if(curmax == 0)
					diff.insert(diff.begin(), must_choose->begin(), must_choose->end());
				else
					diff.assign(must_choose->begin(), must_choose->end());
			}
		} else {
			diff.insert(diff.begin(), core.release_cards.begin(), core.release_cards.end());
			if(arg.extra_release_nonsum_effect == nullptr)
				diff.insert(diff.begin(), core.release_cards_ex_oneof.begin(), core.release_cards_ex_oneof.end());
		}
		std::sort(diff.begin(), diff.end(), card_sort());
		core.select_cards.clear();
		std::set_difference(diff.begin(), diff.end(), core.unselect_cards.begin(), core.unselect_cards.end(),
							std::inserter(core.select_cards, core.select_cards.begin()), card_sort());

		auto message = pduel->new_message(MSG_HINT);
		message->write<uint8_t>(HINT_SELECTMSG);
		message->write<uint8_t>(playerid);
		message->write<uint64_t>(500);
		emplace_process<Processors::SelectUnselectCard>(playerid, finishable || core.operated_set.empty(), min, max, finishable);
		return FALSE;
	}
	case 3: {
		if(return_cards.canceled && (core.operated_set.empty() || (int32_t)core.operated_set.size() >= min)) {
			if(arg.extra_release_nonsum_effect)
				core.dec_count_reserve.push_back(arg.extra_release_nonsum_effect);
			return_cards.list.clear();
			std::copy(core.operated_set.begin(), core.operated_set.end(), std::back_inserter(return_cards.list));
			if(core.operated_set.size())
				return_cards.canceled = false;
			return TRUE;
		}
		card* pcard = return_cards.list[0];
		if(core.operated_set.find(pcard) == core.operated_set.end()) {
			core.operated_set.insert(pcard);
			if(core.release_cards_ex_oneof.find(pcard) != core.release_cards_ex_oneof.end())
				arg.extra_release_nonsum_effect = pcard->is_affected_by_effect(EFFECT_EXTRA_RELEASE_NONSUM);
		} else {
			core.operated_set.erase(pcard);
			if(core.release_cards_ex_oneof.find(pcard) != core.release_cards_ex_oneof.end())
				arg.extra_release_nonsum_effect = nullptr;
		}
		if((int32_t)core.operated_set.size() == max) {
			return_cards.list.clear();
			std::copy(core.operated_set.begin(), core.operated_set.end(), std::back_inserter(return_cards.list));
			if(arg.extra_release_nonsum_effect)
				core.dec_count_reserve.push_back(arg.extra_release_nonsum_effect);
			return TRUE;
		}
		arg.step = 1;
		return FALSE;
	}
	}
	return TRUE;
}
bool field::process(Processors::SelectTribute& arg) {
	auto target = arg.target;
	auto playerid = arg.playerid;
	auto cancelable = arg.cancelable;
	auto min = arg.min;
	auto max = arg.max;
	auto toplayer = arg.toplayer;
	auto zone = arg.zone;
	switch(arg.step) {
	case 0: {
		core.operated_set.clear();
		zone &= (0x1f & get_forced_zones(target, toplayer, LOCATION_MZONE, playerid, LOCATION_REASON_TOFIELD));
		int32_t ct = get_tofield_count(target, toplayer, LOCATION_MZONE, playerid, LOCATION_REASON_TOFIELD, zone);
		if(ct > 0) {
			auto message = pduel->new_message(MSG_HINT);
			message->write<uint8_t>(HINT_SELECTMSG);
			message->write<uint8_t>(playerid);
			message->write<uint64_t>(500);
			if(core.release_cards_ex.size() + core.release_cards_ex_oneof.size() == 0) {
				core.select_cards.clear();
				for(auto& pcard : core.release_cards)
					core.select_cards.push_back(pcard);
				emplace_process<Processors::SelectTributeP>(playerid, cancelable, min, max);
				return TRUE;
			}
			if(core.release_cards_ex.size() >= (uint32_t)max) {
				core.select_cards.clear();
				for(auto& pcard : core.release_cards_ex)
					core.select_cards.push_back(pcard);
				emplace_process<Processors::SelectTributeP>(playerid, cancelable, min, max);
				return TRUE;
			}
		}
		return FALSE;
	}
	case 1: {
		int32_t rmax = 0;
		zone &= (0x1f & get_forced_zones(target, toplayer, LOCATION_MZONE, playerid, LOCATION_REASON_TOFIELD));
		int32_t ct = get_tofield_count(target, toplayer, LOCATION_MZONE, playerid, LOCATION_REASON_TOFIELD, zone);
		card_set must_choose_one;
		for(auto& pcard : core.release_cards) {
			//////////kdiy/////////
			//if((pcard->current.location == LOCATION_MZONE && pcard->current.controler == toplayer && ((zone >> pcard->current.sequence) & 1)))
			if((((pcard->current.location == LOCATION_MZONE && !pcard->is_affected_by_effect(EFFECT_SANCT_MZONE)) || (pcard->current.location == LOCATION_SZONE && pcard->is_affected_by_effect(EFFECT_ORICA_SZONE))) && pcard->current.controler == toplayer && ((zone >> pcard->current.sequence) & 1)))
			//////////kdiy/////////			
				if(ct <= 0)
					must_choose_one.insert(pcard);
			rmax += (pcard)->release_param;
		}
		auto message = pduel->new_message(MSG_HINT);
		message->write<uint8_t>(HINT_SELECTMSG);
		message->write<uint8_t>(playerid);
		message->write<uint64_t>(500);
		if(core.release_cards_ex.empty() && core.release_cards_ex_oneof.empty() && min > rmax) {
			if(rmax > 0) {
				core.select_cards.clear();
				for(auto& pcard : core.release_cards)
					core.select_cards.push_back(pcard);
			}
			emplace_process<Processors::SelectTributeP>(playerid, cancelable, min, max);
			return TRUE;
		}
		bool force = !must_choose_one.empty();
		core.select_cards.clear();
		core.unselect_cards.clear();
		for(auto it = core.release_cards_ex_oneof.begin(); it != core.release_cards_ex_oneof.end();) {
			auto pcard = (*it);
			if(core.release_cards.find(pcard) != core.release_cards.end() || core.release_cards_ex.find(pcard) != core.release_cards_ex.end())
				it = core.release_cards_ex_oneof.erase(it);
			else
				++it;
		}
		if(max - core.release_cards_ex.size() == 1 && force) {
			for(auto& pcard : must_choose_one)
				core.select_cards.push_back(pcard);
			for(auto& pcard : core.release_cards_ex)
				core.select_cards.push_back(pcard);
		} else if(max <= 1 && force) {
			for(auto& pcard : must_choose_one)
				core.select_cards.push_back(pcard);
		} else {
			for(auto& pcard : core.release_cards)
				core.select_cards.push_back(pcard);
			for(auto& pcard : core.release_cards_ex)
				core.select_cards.push_back(pcard);
			for(auto& pcard : core.release_cards_ex_oneof)
				core.select_cards.push_back(pcard);
		}
		emplace_process<Processors::SelectUnselectCard>(playerid, cancelable, min, max, false);
		arg.step = 2;
		arg.must_choose_one.swap(must_choose_one);
		return FALSE;
	}
	case 2: {
		uint32_t rmin = static_cast<uint32_t>(core.operated_set.size());
		uint32_t rmax = 0;
		for(auto& pcard : core.operated_set)
			rmax += pcard->release_param;
		auto oldmin = min;
		auto oldmax = max;
		if(rmax > min)
			min = 0;
		else
			min -= rmax;

		if(rmin > max)
			max = 0;
		else
			max -= rmin;
		auto& must_choose_one = arg.must_choose_one;
		bool force = !must_choose_one.empty();
		for(auto& pcard : must_choose_one) {
			if(core.operated_set.find(pcard) != core.operated_set.end())
				force = false;
		}
		core.select_cards.clear();
		core.unselect_cards.clear();
		int32_t exsize = 0;
		for(auto& pcard : core.release_cards_ex)
			if(core.operated_set.find(pcard) == core.operated_set.end())
				++exsize;
		if(max - exsize == 1 && force) {
			for(auto& pcard : must_choose_one)
				if(core.operated_set.find(pcard) == core.operated_set.end())
					core.select_cards.push_back(pcard);
			for(auto& pcard : core.release_cards_ex)
				if(core.operated_set.find(pcard) == core.operated_set.end())
					core.select_cards.push_back(pcard);
		} else if(max <= 1 && force) {
			for(auto& pcard : must_choose_one)
				core.select_cards.push_back(pcard);
		} else if(exsize && exsize == max) {
			for(auto& pcard : core.release_cards_ex)
				if(core.operated_set.find(pcard) == core.operated_set.end())
					core.select_cards.push_back(pcard);
		} else {
			for(auto& pcard : core.release_cards)
				if(core.operated_set.find(pcard) == core.operated_set.end())
					core.select_cards.push_back(pcard);
			for(auto& pcard : core.release_cards_ex)
				if(core.operated_set.find(pcard) == core.operated_set.end())
					core.select_cards.push_back(pcard);
			if(!arg.extra_release_effect)
				for(auto& pcard : core.release_cards_ex_oneof)
					if(core.operated_set.find(pcard) == core.operated_set.end())
						core.select_cards.push_back(pcard);
		}
		auto canc = (rmin == 0 && cancelable);
		auto finishable = min == 0 && !force && !exsize;
		for(auto& pcard : core.operated_set)
			core.unselect_cards.push_back(pcard);
		auto message = pduel->new_message(MSG_HINT);
		message->write<uint8_t>(HINT_SELECTMSG);
		message->write<uint8_t>(playerid);
		message->write<uint64_t>(500);
		emplace_process<Processors::SelectUnselectCard>(playerid, canc, oldmin, oldmax, finishable);
		return FALSE;
	}
	case 3: {
		if(return_cards.canceled && core.operated_set.empty())
			return TRUE;
		if(!return_cards.canceled) {
			card* pcard = return_cards.list[0];
			if(core.operated_set.find(pcard) == core.operated_set.end()) {
				core.operated_set.insert(pcard);
				if(core.release_cards_ex_oneof.find(pcard) != core.release_cards_ex_oneof.end())
					arg.extra_release_effect = pcard->is_affected_by_effect(EFFECT_EXTRA_RELEASE_SUM);
			} else {
				core.operated_set.erase(pcard);
				if(core.release_cards_ex_oneof.find(pcard) != core.release_cards_ex_oneof.end())
					arg.extra_release_effect = nullptr;
			}
		}
		uint32_t rmin = static_cast<uint32_t>(core.operated_set.size());
		uint32_t rmax = 0;
		for(auto& pcard : core.operated_set)
			rmax += pcard->release_param;
		if(rmax > min)
			min = 0;
		else
			min -= rmax;

		if(rmin > max)
			max = 0;
		else
			max -= rmin;
		if((return_cards.canceled && min <= 0) || !max) {
			return_cards.clear();
			std::copy(core.operated_set.begin(), core.operated_set.end(), std::back_inserter(return_cards.list));
			effect* peffect = arg.extra_release_effect;
			if(peffect)
				peffect->dec_count();
			return TRUE;
		}
		arg.step = 1;
		return FALSE;
	}
	}
	return TRUE;
}
bool field::process(Processors::TossCoin& arg) {
	auto reason_effect = arg.reason_effect;
	auto reason_player = arg.reason_player;
	auto playerid = arg.playerid;
	auto count = arg.count;
	switch(arg.step) {
	case 0: {
		tevent e;
		e.event_cards = nullptr;
		e.event_player = playerid;
		e.event_value = count;
		e.reason = 0;
		e.reason_effect = reason_effect;
		e.reason_player = reason_player;
		core.coin_results.clear();
		auto pr = effects.continuous_effect.equal_range(EFFECT_TOSS_COIN_REPLACE);
		for(auto eit = pr.first; eit != pr.second;) {
			auto* peffect = eit->second;
			++eit;
			auto handler_player = peffect->get_handler_player();
			if(peffect->is_activateable(handler_player, e)) {
				core.coin_results.resize(count);
				solve_continuous(handler_player, peffect, e);
				return TRUE;
			}
		}
		pr = effects.continuous_effect.equal_range(EFFECT_TOSS_COIN_CHOOSE);
		for(auto eit = pr.first; eit != pr.second;) {
			auto* peffect = eit->second;
			++eit;
			auto handler_player = peffect->get_handler_player();
			if(peffect->is_activateable(handler_player, e)) {
				core.coin_results.resize(count);
				solve_continuous(handler_player, peffect, e);
				arg.step = 2;
				return FALSE;
			}
		}
		auto message = pduel->new_message(MSG_TOSS_COIN);
		message->write<uint8_t>(playerid);
		message->write<uint8_t>(count);
		core.coin_results.reserve(count);
		for(int32_t i = 0; i < count; ++i) {
			auto coin = pduel->get_next_integer(0, 1);
			core.coin_results.push_back(static_cast<bool>(coin));
			message->write<uint8_t>(coin);
		}
		raise_event(nullptr, EVENT_TOSS_COIN_NEGATE, reason_effect, 0, reason_player, playerid, count);
		process_instant_event();
		return FALSE;
	}
	case 1: {
		uint8_t heads = 0, tails = 0;
		for(auto result : core.coin_results) {
			heads += (result == COIN_HEADS);
			tails += (result == COIN_TAILS);
		}
		raise_event(nullptr, EVENT_TOSS_COIN, reason_effect, 0, reason_player, playerid, (tails << 16) | (heads << 8) | count);
		process_instant_event();
		return TRUE;
	}
	case 3: {
		arg.step = 0;
		auto message = pduel->new_message(MSG_TOSS_COIN);
		message->write<uint8_t>(playerid);
		message->write<uint8_t>(count);
		for(int32_t i = 0; i < count; ++i) {
			message->write<uint8_t>(static_cast<uint8_t>(core.coin_results[i]));
		}
		raise_event(nullptr, EVENT_TOSS_COIN_NEGATE, reason_effect, 0, reason_player, playerid, count);
		process_instant_event();
		return FALSE;
	}
	}
	return TRUE;
}
bool field::process(Processors::TossDice& arg) {
	auto reason_effect = arg.reason_effect;
	auto reason_player = arg.reason_player;
	auto playerid = arg.playerid;
	auto count1 = arg.count1;
	auto count2 = arg.count2;
	switch(arg.step) {
	case 0: {
		tevent e;
		e.event_cards = nullptr;
		e.event_player = playerid;
		e.event_value = count1 + (count2 << 16);
		e.reason = 0;
		e.reason_effect = reason_effect;
		e.reason_player = reason_player;
		core.dice_results.clear();
		auto pr = effects.continuous_effect.equal_range(EFFECT_TOSS_DICE_REPLACE);
		for(auto eit = pr.first; eit != pr.second;) {
			auto* peffect = eit->second;
			++eit;
			auto handler_player = peffect->get_handler_player();
			if(peffect->is_activateable(handler_player, e)) {
				core.dice_results.resize(count1 + count2);
				solve_continuous(handler_player, peffect, e);
				return TRUE;
			}
		}
		pr = effects.continuous_effect.equal_range(EFFECT_TOSS_DICE_CHOOSE);
		for(auto eit = pr.first; eit != pr.second;) {
			auto* peffect = eit->second;
			++eit;
			auto handler_player = peffect->get_handler_player();
			if(peffect->is_activateable(handler_player, e)) {
				core.dice_results.resize(count1 + count2);
				solve_continuous(handler_player, peffect, e);
				arg.step = 2;
				return FALSE;
			}
		}
		core.dice_results.reserve(count1 + count2);
		auto message = pduel->new_message(MSG_TOSS_DICE);
		message->write<uint8_t>(playerid);
		message->write<uint8_t>(count1);
		for(int32_t i = 0; i < count1; ++i) {
			const auto dice = pduel->get_next_integer(1, 6);
			core.dice_results.push_back(dice);
			message->write<uint8_t>(dice);
		}
		if(count2 > 0) {
			message = pduel->new_message(MSG_TOSS_DICE);
			message->write<uint8_t>(1 - playerid);
			message->write<uint8_t>(count2);
			for(int32_t i = 0; i < count2; ++i) {
				const auto dice = pduel->get_next_integer(1, 6);
				core.dice_results.push_back(dice);
				message->write<uint8_t>(dice);
			}
		}
		raise_event(nullptr, EVENT_TOSS_DICE_NEGATE, reason_effect, 0, reason_player, playerid, count1 + (count2 << 16));
		process_instant_event();
		return FALSE;
	}
	case 1: {
		raise_event(nullptr, EVENT_TOSS_DICE, reason_effect, 0, reason_player, playerid, count1 + (count2 << 16));
		process_instant_event();
		return TRUE;
	}
	case 3: {
		arg.step = 0;
		auto message = pduel->new_message(MSG_TOSS_DICE);
		message->write<uint8_t>(playerid);
		message->write<uint8_t>(count1);
		for(int32_t i = 0; i < count1; ++i) {
			message->write<uint8_t>(core.dice_results[i]);
		}
		if(count2 > 0) {
			auto mmessage = pduel->new_message(MSG_TOSS_DICE);
			mmessage->write<uint8_t>(1 - playerid);
			mmessage->write<uint8_t>(count2);
			for(int32_t i = 0; i < count2; ++i) {
				mmessage->write<uint8_t>(core.dice_results[count1 + i]);
			}
		}
		raise_event(nullptr, EVENT_TOSS_DICE_NEGATE, reason_effect, 0, reason_player, playerid, count1 + (count2 << 16));
		process_instant_event();
		return FALSE;
	}
	}
	return TRUE;
}
