#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "lwm2m/coap.hpp"
#include "lwm2m/tlv.hpp"
#include "lwm2m/senml.hpp"
#include "lwm2m/model.hpp"
#include "lwm2m/engine.hpp"

using namespace lwm2m;

static int failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);    \
            ++failures;                                                    \
        }                                                                  \
    } while (0)

// Build a CoAP request whose Uri-Path options spell out `path` (e.g. "/3/0/13").
static coap::Message get(const std::string& path) {
    coap::Message m;
    m.type = coap::Type::Con;
    m.code = coap::Code::Get;
    m.id = 0x1234;
    m.token = "ab";
    std::size_t pos = 1;
    while (pos <= path.size()) {
        std::size_t slash = path.find('/', pos);
        if (slash == std::string::npos) slash = path.size();
        m.options.push_back({coap::OPT_URI_PATH, path.substr(pos, slash - pos)});
        pos = slash + 1;
    }
    return m;
}

static void test_coap_roundtrip() {
    coap::Message m;
    m.type = coap::Type::Con;
    m.code = coap::Code::Get;
    m.id = 0x0BAD;
    m.token = std::string("\x01\x02", 2);
    m.options.push_back({coap::OPT_URI_PATH, "3"});
    m.options.push_back({coap::OPT_URI_PATH, "0"});
    m.options.push_back({coap::OPT_URI_PATH, "13"});
    m.options.push_back({coap::OPT_ACCEPT, std::string(1, '\x00')});
    m.payload = "hello";

    const std::string wire = coap::encode(m);
    CHECK(!wire.empty());

    coap::Message d;
    CHECK(coap::decode(wire, d));
    CHECK(d.type == coap::Type::Con);
    CHECK(d.code == coap::Code::Get);
    CHECK(d.id == 0x0BAD);
    CHECK(d.token == m.token);
    CHECK(d.uri_path() == "/3/0/13");
    CHECK(d.payload == "hello");
    CHECK(d.find_option(coap::OPT_ACCEPT) != nullptr);
    CHECK(*d.find_option(coap::OPT_ACCEPT) == std::string(1, '\x00'));

    // Code class/detail helpers.
    CHECK(coap::code_class(coap::Code::Content) == 2);
    CHECK(coap::code_detail(coap::Code::Content) == 5);
}

static void test_tlv_roundtrip() {
    std::vector<tlv::Record> recs;
    recs.push_back({tlv::TYPE_RESOURCE, 0, "Acme"});
    recs.push_back({tlv::TYPE_RESOURCE, 13, "42"});

    const std::string wire = tlv::encode_instance(0, recs);
    std::vector<tlv::Record> decoded;
    CHECK(tlv::decode(wire, decoded));
    const auto leafs = tlv::resources(decoded);
    CHECK(leafs.size() == 2);
    CHECK(leafs[0].id == 0 && leafs[0].value == "Acme");
    CHECK(leafs[1].id == 13 && leafs[1].value == "42");

    // Single-resource record.
    const std::string single = tlv::encode_resource(13, "hi");
    std::vector<tlv::Record> sr;
    CHECK(tlv::decode(single, sr));
    CHECK(sr.size() == 1 && sr[0].type == tlv::TYPE_RESOURCE);
    CHECK(sr[0].id == 13 && sr[0].value == "hi");
}

static void test_senml_roundtrip() {
    std::vector<senml::Record> recs;

    senml::Record r;
    r.baseName = "/3/0/";
    r.name = "13";
    r.kind = senml::Kind::Numeric;
    r.numericValue = 42;
    r.isFloat = false;
    recs.push_back(r);

    senml::Record r2;
    r2.baseName = "/3/0/";
    r2.name = "0";
    r2.kind = senml::Kind::String;
    r2.stringValue = "open";
    recs.push_back(r2);

    const std::string json = senml::encode_json(recs);
    CHECK(!json.empty());

    std::vector<senml::Record> back;
    CHECK(senml::decode_json(json, back));
    CHECK(back.size() == 2);
    CHECK(back[0].path() == "/3/0/13");
    CHECK(back[0].kind == senml::Kind::Numeric);
    CHECK(back[0].numericValue == 42);
    CHECK(back[1].kind == senml::Kind::String);
    CHECK(back[1].stringValue == "open");

    // Mismatched base names must be rejected.
    recs[1].baseName = "/4/0/";
    CHECK(senml::encode_json(recs).empty());
}

static void test_engine_read() {
    Engine e;

    ObjectDescriptor od;
    od.oid = 3;
    od.name = "Device";
    ObjectInstance inst;
    inst.iid = 0;

    Resource r13;
    r13.rid = 13;
    r13.type = ResourceType::Integer;
    r13.ops = Operations::R;
    r13.read = []() { return "42"; };
    inst.resources[13] = r13;

    Resource r0;
    r0.rid = 0;
    r0.type = ResourceType::String;
    r0.ops = Operations::R;
    r0.read = []() { return "Acme"; };
    inst.resources[0] = r0;

    od.instances[0] = std::move(inst);
    e.store().add_object(std::move(od));

    // Resource read -> plain text.
    auto resp = e.handle(get("/3/0/13"));
    CHECK(resp.size() == 1);
    coap::Message d;
    CHECK(coap::decode(resp[0], d));
    CHECK(d.code == coap::Code::Content);
    CHECK(d.payload == "42");

    // Object read -> TLV.
    auto resp2 = e.handle(get("/3"));
    CHECK(resp2.size() == 1);
    coap::Message d2;
    CHECK(coap::decode(resp2[0], d2));
    CHECK(d2.code == coap::Code::Content);
    CHECK(d2.find_option(coap::OPT_CONTENT_FORMAT) != nullptr);

    // Missing resource -> 4.04.
    auto resp3 = e.handle(get("/3/0/99"));
    coap::Message d3;
    CHECK(coap::decode(resp3[0], d3));
    CHECK(d3.code == coap::Code::NotFound);

    // Unknown object -> 4.04.
    auto resp4 = e.handle(get("/99"));
    coap::Message d4;
    CHECK(coap::decode(resp4[0], d4));
    CHECK(d4.code == coap::Code::NotFound);
}

static void test_engine_write_notify() {
    Engine e;

    std::string value = "off";

    ObjectDescriptor od;
    od.oid = 3;
    od.name = "Device";
    ObjectInstance inst;
    inst.iid = 0;

    Resource r;
    r.rid = 13;
    r.type = ResourceType::Integer;
    r.ops = Operations::RW;
    r.observable = true;
    r.read = [&]() { return value; };
    r.write = [&](const std::string& v) { value = v; return 0; };
    inst.resources[13] = r;

    od.instances[0] = std::move(inst);
    e.store().add_object(std::move(od));

    // Observe register.
    coap::Message reg = get("/3/0/13");
    reg.token = "tok1";
    reg.options.insert(reg.options.begin(), {coap::OPT_OBSERVE, std::string(1, '\0')});
    auto resp = e.handle(reg);
    CHECK(resp.size() == 1);
    coap::Message d;
    CHECK(coap::decode(resp[0], d));
    CHECK(d.code == coap::Code::Content);
    CHECK(d.find_option(coap::OPT_OBSERVE) != nullptr);
    CHECK(d.payload == "off");

    // Write -> 2.04 Changed plus a Notify frame to the observer.
    coap::Message w;
    w.type = coap::Type::Con;
    w.code = coap::Code::Put;
    w.id = 0x2222;
    w.token = "x";
    w.options.push_back({coap::OPT_URI_PATH, "3"});
    w.options.push_back({coap::OPT_URI_PATH, "0"});
    w.options.push_back({coap::OPT_URI_PATH, "13"});
    w.payload = "on";

    auto resp2 = e.handle(w);
    CHECK(resp2.size() >= 2);

    coap::Message d2;
    CHECK(coap::decode(resp2[0], d2));
    CHECK(d2.code == coap::Code::Changed);
    CHECK(value == "on");

    coap::Message n;
    CHECK(coap::decode(resp2[1], n));
    CHECK(n.code == coap::Code::Content);
    CHECK(n.token == "tok1");
    CHECK(n.payload == "on");
    CHECK(n.find_option(coap::OPT_OBSERVE) != nullptr);
}

static void test_engine_execute() {
    Engine e;
    int calls = 0;

    ObjectDescriptor od;
    od.oid = 3;
    od.name = "Device";
    ObjectInstance inst;
    inst.iid = 0;

    Resource r;
    r.rid = 4;
    r.type = ResourceType::None;
    r.ops = Operations::E;
    r.execute = [&](const std::string&) { ++calls; return 0; };
    inst.resources[4] = r;

    od.instances[0] = std::move(inst);
    e.store().add_object(std::move(od));

    coap::Message req;
    req.type = coap::Type::Con;
    req.code = coap::Code::Post;
    req.id = 0x3333;
    req.token = "e";
    req.options.push_back({coap::OPT_URI_PATH, "3"});
    req.options.push_back({coap::OPT_URI_PATH, "0"});
    req.options.push_back({coap::OPT_URI_PATH, "4"});

    auto resp = e.handle(req);
    coap::Message d;
    CHECK(coap::decode(resp[0], d));
    CHECK(d.code == coap::Code::Changed);
    CHECK(calls == 1);
}

static void test_send() {
    std::vector<senml::Record> recs;
    senml::Record r;
    r.baseName = "/3/0/";
    r.name = "13";
    r.kind = senml::Kind::Numeric;
    r.numericValue = 7;
    r.isFloat = false;
    recs.push_back(r);

    const std::string wire = Engine::build_send(0x0102, "t", recs);
    coap::Message d;
    CHECK(coap::decode(wire, d));
    CHECK(d.code == coap::Code::Post);
    CHECK(d.uri_path() == "/dp");
    CHECK(d.find_option(coap::OPT_CONTENT_FORMAT) != nullptr);

    std::vector<senml::Record> back;
    CHECK(senml::decode_json(d.payload, back));
    CHECK(back.size() == 1 && back[0].path() == "/3/0/13");
    CHECK(back[0].numericValue == 7);
}

int main() {
    test_coap_roundtrip();
    test_tlv_roundtrip();
    test_senml_roundtrip();
    test_engine_read();
    test_engine_write_notify();
    test_engine_execute();
    test_send();

    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("all tests passed\n");
    return 0;
}
