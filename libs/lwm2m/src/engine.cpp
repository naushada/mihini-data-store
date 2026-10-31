#include "lwm2m/engine.hpp"

#include "lwm2m/tlv.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace lwm2m {

namespace {

// Split "/3/0/13" into numeric segments. Empty if any segment is empty or
// non-numeric (so "/dp" and other non-numeric paths are rejected by the caller).
std::vector<std::int32_t> path_segments(const std::string& path) {
    std::vector<std::int32_t> out;
    if (path.empty() || path[0] != '/') return out;

    std::size_t pos = 1;
    while (pos <= path.size()) {
        std::size_t slash = path.find('/', pos);
        if (slash == std::string::npos) slash = path.size();
        const std::string seg = path.substr(pos, slash - pos);
        if (seg.empty()) return {};

        std::int64_t v = 0;
        for (char c : seg) {
            if (c < '0' || c > '9') return {};
            v = v * 10 + (c - '0');
            if (v > 0xffffffffLL) return {};
        }
        out.push_back(static_cast<std::int32_t>(v));
        pos = slash + 1;
    }
    return out;
}

// Minimal big-endian integer encoding for option values (content format, observe).
std::string encode_uint(std::uint32_t v) {
    std::string out;
    do {
        out.insert(out.begin(), static_cast<char>(v & 0xff));
        v >>= 8;
    } while (v != 0);
    return out;
}

// TLV for every readable resource in one object instance.
std::string instance_tlv(const ObjectInstance& inst) {
    std::vector<tlv::Record> recs;
    for (const auto& [rid, res] : inst.resources) {
        if (!res.read) continue;
        recs.push_back({tlv::TYPE_RESOURCE, static_cast<std::uint16_t>(rid), res.read()});
    }
    return tlv::encode_instance(static_cast<std::uint16_t>(inst.iid), recs);
}

} // namespace

Engine::Observer* Engine::find_observer(const std::string& token) {
    for (auto& o : observers_) {
        if (o.token == token) return &o;
    }
    return nullptr;
}

std::vector<std::string> Engine::handle(const coap::Message& req) {
    const std::string path = req.uri_path();

    // Send (POST /dp) is an outbound push from this endpoint; an inbound one is
    // not part of the read/write/execute surface, so acknowledge and stop.
    if (path == "/dp" && req.code == coap::Code::Post) {
        coap::Message resp;
        resp.type = coap::Type::Ack;
        resp.id = req.id;
        resp.token = req.token;
        resp.code = coap::Code::Changed;
        return {coap::encode(resp)};
    }

    const auto seg = path_segments(path);

    bool observe = false;
    if (const std::string* o = req.find_option(coap::OPT_OBSERVE)) {
        if (*o == std::string(1, '\0')) {
            observe = true;
        } else if (*o == std::string(1, '\x01')) {
            // Observe deregistration: drop the observer, then answer as a plain read.
            for (auto it = observers_.begin(); it != observers_.end(); ++it) {
                if (it->token == req.token) {
                    observers_.erase(it);
                    break;
                }
            }
        }
    }

    switch (req.code) {
        case coap::Code::Get: return handle_read(req, seg, observe);
        case coap::Code::Put: return handle_write(req, seg);
        case coap::Code::Post: return handle_post(req, seg);
        default: {
            coap::Message resp;
            resp.type = coap::Type::Ack;
            resp.id = req.id;
            resp.token = req.token;
            resp.code = coap::Code::MethodNotAllowed;
            return {coap::encode(resp)};
        }
    }
}

std::vector<std::string> Engine::handle_read(const coap::Message& req,
                                             const std::vector<std::int32_t>& seg,
                                             bool observe) {
    coap::Message resp;
    resp.type = coap::Type::Ack;
    resp.id = req.id;
    resp.token = req.token;

    if (seg.empty()) {
        resp.code = coap::Code::NotFound;
        return {coap::encode(resp)};
    }

    const std::uint32_t oid = static_cast<std::uint32_t>(seg[0]);
    const std::uint32_t iid = seg.size() >= 2 ? static_cast<std::uint32_t>(seg[1]) : 0;
    const std::uint32_t rid = seg.size() >= 3 ? static_cast<std::uint32_t>(seg[2]) : 0;

    if (!store_.has(oid)) {
        resp.code = coap::Code::NotFound;
        return {coap::encode(resp)};
    }

    // Register the observer first so the Observe option (number 6) sorts before
    // the Content-Format option (number 12) added below.
    if (observe) {
        Observer* obs = find_observer(req.token);
        if (!obs) {
            observers_.push_back({req.token, oid, iid, rid, 0});
        } else {
            obs->oid = oid;
            obs->iid = iid;
            obs->rid = rid;
            obs->seq = 0;
        }
        resp.options.push_back({coap::OPT_OBSERVE, std::string(1, '\0')});
    }

    if (seg.size() == 1) {
        // Object read: TLV over every instance.
        const auto& od = *store_.find(oid);
        std::string payload;
        for (const auto& [_, inst] : od.instances) {
            payload += instance_tlv(inst);
        }
        resp.code = coap::Code::Content;
        resp.options.push_back({coap::OPT_CONTENT_FORMAT, encode_uint(coap::CF_TLV)});
        resp.payload = std::move(payload);
    } else if (seg.size() == 2) {
        const auto* inst = store_.find(oid, iid);
        if (!inst) {
            resp.code = coap::Code::NotFound;
            return {coap::encode(resp)};
        }
        resp.code = coap::Code::Content;
        resp.options.push_back({coap::OPT_CONTENT_FORMAT, encode_uint(coap::CF_TLV)});
        resp.payload = instance_tlv(*inst);
    } else {
        const auto* res = store_.find(oid, iid, rid);
        if (!res || !res->read) {
            resp.code = coap::Code::NotFound;
            return {coap::encode(resp)};
        }
        resp.code = coap::Code::Content;
        resp.payload = res->read();
    }

    return {coap::encode(resp)};
}

std::vector<std::string> Engine::handle_write(const coap::Message& req,
                                              const std::vector<std::int32_t>& seg) {
    coap::Message resp;
    resp.type = coap::Type::Ack;
    resp.id = req.id;
    resp.token = req.token;

    if (seg.empty()) {
        resp.code = coap::Code::NotFound;
        return {coap::encode(resp)};
    }

    const std::uint32_t oid = static_cast<std::uint32_t>(seg[0]);
    const std::uint32_t iid = seg.size() >= 2 ? static_cast<std::uint32_t>(seg[1]) : 0;
    const std::uint32_t rid = seg.size() >= 3 ? static_cast<std::uint32_t>(seg[2]) : 0;

    if (seg.size() >= 3) {
        Resource* res = store_.find(oid, iid, rid);
        if (!res || !res->write) {
            resp.code = coap::Code::NotFound;
            return {coap::encode(resp)};
        }
        std::string value = req.payload;
        std::vector<tlv::Record> recs;
        if (tlv::decode(req.payload, recs) && recs.size() == 1 &&
            recs[0].type == tlv::TYPE_RESOURCE) {
            value = recs[0].value;
        }
        const int rc = res->write(value);
        if (rc == 0) {
            resp.code = coap::Code::Changed;
            auto notes = notify(oid, iid, rid);
            notes.insert(notes.begin(), coap::encode(resp));
            return notes;
        }
        resp.code = static_cast<coap::Code>(rc);
        return {coap::encode(resp)};
    }

    if (seg.size() == 2) {
        ObjectInstance* inst = store_.find(oid, iid);
        if (!inst) {
            resp.code = coap::Code::NotFound;
            return {coap::encode(resp)};
        }
        std::vector<tlv::Record> recs;
        if (!tlv::decode(req.payload, recs)) {
            resp.code = coap::Code::BadRequest;
            return {coap::encode(resp)};
        }
        for (const auto& leaf : tlv::resources(recs)) {
            Resource* res = store_.find(oid, iid, leaf.id);
            if (res && res->write) res->write(leaf.value);
        }
        resp.code = coap::Code::Changed;
        auto notes = notify(oid, iid, 0);
        notes.insert(notes.begin(), coap::encode(resp));
        return notes;
    }

    resp.code = coap::Code::MethodNotAllowed;
    return {coap::encode(resp)};
}

std::vector<std::string> Engine::handle_post(const coap::Message& req,
                                             const std::vector<std::int32_t>& seg) {
    coap::Message resp;
    resp.type = coap::Type::Ack;
    resp.id = req.id;
    resp.token = req.token;

    if (seg.size() < 3) {
        resp.code = coap::Code::MethodNotAllowed;
        return {coap::encode(resp)};
    }

    const std::uint32_t oid = static_cast<std::uint32_t>(seg[0]);
    const std::uint32_t iid = static_cast<std::uint32_t>(seg[1]);
    const std::uint32_t rid = static_cast<std::uint32_t>(seg[2]);

    Resource* res = store_.find(oid, iid, rid);
    if (!res || !res->execute) {
        resp.code = coap::Code::NotFound;
        return {coap::encode(resp)};
    }

    const int rc = res->execute(req.payload);
    if (rc == 0) {
        resp.code = coap::Code::Changed;
        return {coap::encode(resp)};
    }
    resp.code = static_cast<coap::Code>(rc);
    return {coap::encode(resp)};
}

std::vector<std::string> Engine::notify(std::uint32_t oid, std::uint32_t iid,
                                        std::uint32_t rid) {
    std::vector<std::string> out;
    for (auto& o : observers_) {
        if (o.oid != oid) continue;
        if (o.iid != 0 && iid != 0 && o.iid != iid) continue;
        if (o.rid != 0 && rid != 0 && o.rid != rid) continue;

        coap::Message msg;
        msg.type = coap::Type::Non;
        msg.code = coap::Code::Content;
        msg.token = o.token;
        o.seq++;
        msg.options.push_back({coap::OPT_OBSERVE, encode_uint(o.seq)});

        if (o.rid != 0) {
            const auto* res = store_.find(o.oid, o.iid, o.rid);
            if (res && res->read) msg.payload = res->read();
        } else if (o.iid != 0) {
            const auto* inst = store_.find(o.oid, o.iid);
            if (inst) {
                msg.options.push_back({coap::OPT_CONTENT_FORMAT, encode_uint(coap::CF_TLV)});
                msg.payload = instance_tlv(*inst);
            }
        } else {
            const auto* od = store_.find(o.oid);
            if (od) {
                std::string payload;
                for (const auto& [_, inst] : od->instances) {
                    payload += instance_tlv(inst);
                }
                msg.options.push_back({coap::OPT_CONTENT_FORMAT, encode_uint(coap::CF_TLV)});
                msg.payload = std::move(payload);
            }
        }
        out.push_back(coap::encode(msg));
    }
    return out;
}

std::string Engine::build_send(std::uint16_t msgid, const std::string& token,
                               const std::vector<senml::Record>& records) {
    coap::Message msg;
    msg.type = coap::Type::Con;
    msg.code = coap::Code::Post;
    msg.id = msgid;
    msg.token = token;
    msg.options.push_back({coap::OPT_URI_PATH, "dp"});
    msg.options.push_back({coap::OPT_CONTENT_FORMAT, encode_uint(coap::CF_SENML_JSON)});
    msg.payload = senml::encode_json(records);
    return coap::encode(msg);
}

} // namespace lwm2m
