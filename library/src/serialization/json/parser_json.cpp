#include "parser_json.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "../define_retry.h"
#include "er/reflection/reflection.h"
#include "er/types/all_types.h"
#include "er/variable/box.h"

using namespace er;

ParserJson::ParserJson(const char* input, size_t input_size)  //
    : LexerJson(input, input_size) {
}

ParserJson::ParserJson(std::istream& stream)  //
    : LexerJson(stream) {
}

Expected<None> ParserJson::deserialize(TypeInfo* info) {
  return parse_next(info);
}

Expected<None> ParserJson::parse(TypeInfo* info, char token) {
  switch (token) {
    case '0':
      // do nothing
      return None();
    case 't':
      return info->get<Bool>().set(true);
    case 'f':
      return info->get<Bool>().set(false);
    case 'n':
      return info->match(
          [this](Integer& i) -> Expected<None> {
            auto w = get_word();

            if (w.front() == '-' || i.is_signed()) {
              return i.set_signed(std::strtoll(&w[0], nullptr, 10));
            }
            return i.set_unsigned(std::strtoull(&w[0], nullptr, 10));
          },
          [this](Floating& f) -> Expected<None> { return f.set(parse_double(get_word())); },
          [this](auto&&) -> Expected<None> { return error_match(); });
    case '$':
      return info->match([this](String& s) -> Expected<None> { return s.set(get_word()); },
                         [this](Enum& e) -> Expected<None> { return e.parse(get_word()); },
                         [this](auto&&) -> Expected<None> { return error_match(); });
    case '[':
      // clang-format off
        return info->match(
            [this](Array& a) -> Expected<None> {
              return parse_array(a.nested_type(), [&](size_t i, Var var) -> Expected<None> {
                if (i < a.size()) {
                  auto item = a.at(i).unwrap();
                  return reflection::copy(item, var);
                }
                return None();
              });
            },
            [this](Sequence& s) -> Expected<None> {
              s.clear();
              return parse_array(s.nested_type(), [&](size_t, Var var) {
                return s.push(var);
              });
            },
            [this](Map& m) -> Expected<None> {
              return parse_map(m);
            },
            [this](auto&&) -> Expected<None> {
              return error_match();
            });
      // clang-format on
    case '{':
      return parse_object(info);
    default:
      return error_token(token);
  }
}

Expected<None> ParserJson::parse_next(TypeInfo* info) {
  next();
  return parse(info, _token);
}

Expected<None> ParserJson::parse_array(TypeId nested_type, std::function<Expected<None>(size_t, Var)> add) {
  next();  // skip '['
  if (_token == ']') {
    // an empty array
    return None();
  }

  Box box(nested_type);
  auto boxed_info = reflection::reflect(box.var());

  for (size_t i = 0; /**/; ++i) {
    auto ex = parse(&boxed_info, _token)
                  .match_move([&, i](None&&) -> Expected<None> { return add(i, box.var()); },
                              [](Error&& err) -> Expected<None> { return err; });
    __retry(ex);

    next();
    if (_token == ']') {
      return None();
    }
    if (_token != ',') {
      return error_token(_token);
    }

    // get another one
    next();
  }

  return None();
}

Expected<None> ParserJson::parse_object(TypeInfo* info) {
  next();  // skip '{'
  if (_token == '}') {
    // an empty object
    return None();
  }

  auto o = info->get<Object>();

  while (true) {
    if (_token != '$') {
      return error("Cannot reach a field name");
    }
    next();
    if (_token != ':') {
      return error("Cannot reach a field value");
    }

    auto field = o.get_field(get_word()).unwrap();
    auto field_info = reflection::reflect(field);
    __retry(parse_next(&field_info));

    next();
    if (_token == '}') {
      return None();
    }
    if (_token != ',') {
      return error_token(_token);
    }

    next();
  }

  return error("Max depth level exceeded");
}

Expected<None> ParserJson::parse_map(Map& map) {
  map.clear();

  next();

  std::string key = "key";
  std::string val = "val";

  if (_token == '$') {
    // if particular tag found parse it
    auto pos = get_word().find("!!map");
    if (pos != std::string::npos) {

      auto kv = parse_tag(get_word());
      __retry(kv);

      auto pair = kv.unwrap();
      key = std::move(pair.first);
      val = std::move(pair.second);

      // make step to get a new token
      next();
      if (_token != ',') {
        return error_token(_token);
      }
      next();
    } else {
      return error("Cannot reach the map tag or '{'");
    }
  } else if (_token != '{') {
    return error_token(_token);
  }

  Box key_box(map.key_type());
  auto key_info = reflection::reflect(key_box.var());

  Box val_box(map.val_type());
  auto val_info = reflection::reflect(val_box.var());

  // token '{' has already been read
  while (true) {
    // parse first field key or val
    next();
    if (_token != '$') {
      return error("Cannot reach a field name");
    }

    next();
    if (_token != ':') {
      return error("Cannot reach a field value");
    }

    if (get_word() == key) {
      __retry(parse_next(&key_info));
    } else if (get_word() == val) {
      __retry(parse_next(&val_info));
    } else {
      return Error(format("Got an unexpected field '{}' while parse map; {}",  //
                          get_word(), get_position().to_string()));
    }

    next();
    if (_token == '}') {
      return error("Unexpected end of JSON object");
    }
    if (_token != ',') {
      return error_token(_token);
    }

    // parse second field key or val
    next();
    if (_token != '$') {
      return error("Cannot reach a field name");
    }

    next();
    if (_token != ':') {
      return error("Cannot reach a field value");
    }

    if (get_word() == key) {
      __retry(parse_next(&key_info));
    } else if (get_word() == val) {
      __retry(parse_next(&val_info));
    } else {
      return Error(format("Got an unexpected field '{}' while parse map; {}",  //
                          get_word(), get_position().to_string()));
    }

    __retry(map.insert(key_box.var(), val_box.var()));

    next();
    if (_token == '}') {
      next();
    }
    if (_token == ']') {
      return None();
    }
    if (_token != ',') {
      return error_token(_token);
    }

    // take next '{'
    next();
  }
}

void ParserJson::next() {
  _token = static_cast<char>(lex());
}

Error ParserJson::error(const char* str) {
  return Error(format("{}; {}", str, get_position().to_string()));
}

Error ParserJson::error_token(char token) {
  return Error(format("Unexpected token '{}'; {}", token, get_position().to_string()));
}

Error ParserJson::error_match() {
  return Error(format("Cannot match correct type; {}", get_position().to_string()));
}

Expected<std::pair<std::string, std::string>> ParserJson::parse_tag(std::string_view str) {
  auto pos1 = str.find('|');
  if (pos1 == std::string::npos) {
    return error("Cannot find '|' in the tag");
  }
  auto pos2 = str.find(':');
  if (pos2 == std::string::npos) {
    return error("Cannot find ':' in the tag");
  }

  auto key = std::string(str.substr(pos1 + 1, pos2 - (pos1 + 1)));
  auto val = std::string(str.substr(pos2 + 1, str.size() - (pos2 + 1)));

  return std::make_pair(std::move(key), std::move(val));
}

double ParserJson::parse_double(std::string_view str) {
  return std::strtod(&str[0], nullptr);
}
