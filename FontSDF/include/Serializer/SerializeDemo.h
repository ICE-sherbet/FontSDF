#pragma once

#include "Serializer.h"

using namespace base::serializer;
struct Player {
  int id;
  std::string name;
  int arrays[5];

  std::vector<int> vec_arrays;
  std::vector<std::string> vec_str_arrays;

  template <typename A>
  void reflect(A& a) {
    a.field("id", id);
    a.field("name", name);
    a.field("array", arrays);
    a.field("vec_array", vec_arrays);
    a.field("vec_str_array", vec_str_arrays);
  }
};

template <>
struct force_reflect<Player> : std::true_type {};

void Hoge() {
  Player player;
  player.id = 123;
  player.name = "Satoshi";
  player.arrays[0] = 5;
  player.arrays[1] = 10;
  player.arrays[2] = 15;
  player.arrays[3] = 20;
  player.arrays[4] = 25;

  player.vec_arrays.emplace_back(1);
  player.vec_str_arrays.emplace_back("Pika");
  player.vec_str_arrays.emplace_back("Pall");

  // Serialize
  uint8_t buffer[1024];
  BinaryWriter writer(buffer);

  // has_member_serialize<Player, Accessor<BinaryWriter>>::value;
  Serialize(writer, player);

  // Deserialize
  Player new_player;
  BinaryReader reader(buffer);
  Serialize(reader, new_player);

  // Check if deserialization was successful
  assert(new_player.id == player.id);
  std::ofstream ofs("player.txt");

  TextWriter textWriter{ofs};
  Serialize(textWriter, new_player);
  ofs.close();
  
  std::ifstream ifs("player.txt");
  TextReader textReader{ifs};
  Player playerTextRead;
  Serialize(textReader, playerTextRead);

  ifs.close();



}