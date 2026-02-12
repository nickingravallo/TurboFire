#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <fstream>
#include <memory>
#include "game/game_tree.h"

namespace poker {

// Binary serialization for solved spots
class Serializer {
public:
    // Save game tree to file
    static bool saveTree(const std::string& filename, const std::shared_ptr<GameTreeNode>& root);
    
    // Load game tree from file
    static std::shared_ptr<GameTreeNode> loadTree(const std::string& filename);
    
    // Serialize strategy
    static std::vector<uint8_t> serializeStrategy(const Strategy& strategy);
    static Strategy deserializeStrategy(const std::vector<uint8_t>& data);
    
private:
    // Binary format version
    static constexpr uint32_t FORMAT_VERSION = 1;
    
    // File header
    struct FileHeader {
        uint32_t version;
        uint32_t num_nodes;
        uint32_t num_info_sets;
        uint64_t timestamp;
    };
    
    // Node serialization
    static void serializeNode(std::ofstream& file, const std::shared_ptr<GameTreeNode>& node);
    static std::shared_ptr<GameTreeNode> deserializeNode(std::ifstream& file);
};

} // namespace poker
