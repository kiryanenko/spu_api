//
// Created by a.kiryanenko on 2019-05-17.
//

#include <algorithm>
#include "Simulator.h"


namespace SPU
{
    map<gsid_t, map<key_t, value_t> *> globalStructures;


    Simulator::Simulator() : BaseStructure() {
        auto gsid = get_gsid();
        auto it = globalStructures.find(gsid);
        if (it != globalStructures.end()) {
            _data = it->second;
        } else {
            _data = new map<key_t, value_t>();
            globalStructures[gsid] = _data;
        }
    }

    Simulator::Simulator(Simulator &obj) = default;

    Simulator::~Simulator() = default;


    adds_rslt_t Simulator::createStructure() {
        auto gsid = getNextGsid();
        adds_rslt_t res;
        res.gsid = gsid;
        res.rslt = OK;
        return res;
    }

    dels_rslt_t Simulator::deleteStructure() {
        globalStructures.erase(get_gsid());
        return dels_rslt_t{.rslt = OK, .power = 0};
    }


    u32 Simulator::get_power() {
        return _data->size();
    }

    status_t Simulator::insert(key_t key, value_t value, flags_t flags) {
        (*_data)[key] = value;
        return OK;
    }

    status_t Simulator::del(key_t key, flags_t flags) {
        _data->erase(key);
        return OK;
    }

    pair_t Simulator::search(key_t key, flags_t flags) {
        auto it = _data->find(key);
        if (it != _data->end()) {
            return {key, it->second};
        } else {
            return pair_t(ERR);
        }
    }

    pair_t Simulator::min(flags_t flags) {
        auto it = std::min_element(_data->begin(), _data->end());
        if (it != _data->end()) {
            return {it->first, it->second};
        } else {
            return pair_t(ERR);
        }
    }

    pair_t Simulator::max(flags_t flags) {
        auto it = std::max_element(_data->begin(), _data->end());
        if (it != _data->end()) {
            return {it->first, it->second};
        } else {
            return pair_t(ERR);
        }
    }

    pair_t Simulator::next(key_t key, flags_t flags) {
        return BaseStructure::next(key, flags);
    }

    pair_t Simulator::prev(key_t key, flags_t flags) {
        return BaseStructure::prev(key, flags);
    }

    pair_t Simulator::nsm(key_t key, flags_t flags) {
        return BaseStructure::nsm(key, flags);
    }

    pair_t Simulator::ngr(key_t key, flags_t flags) {
        return BaseStructure::ngr(key, flags);
    }


    gsid_t getNextGsid() {
        static gsid_t gsid = {0};
        ++gsid;
        return gsid;
    }
}