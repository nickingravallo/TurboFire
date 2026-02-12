#include "storage/serialize.h"
#include <cstring>

namespace poker {

bool Serializer::saveTree(const std::string& filename, const std::shared_ptr<GameTreeNode>& root) {
    std::ofstream file(filename, std::ios::binary);
    if (!file.good()) return false;
    
    // Write header
    FileHeader header;
    header.version = FORMAT_VERSION;
    header.num_nodes = 0; // Would count nodes
    header.num_info_sets = 0;
    header.timestamp = 0;
    
    file.write(reinterpret_cast<const char*>(&header), sizeof(header));
    
    // Serialize tree
    if (root) {
        serializeNode(file, root);
    }
    
    return file.good();
}

std::shared_ptr<GameTreeNode> Serializer::loadTree(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file.good()) return nullptr;
    
    // Read header
    FileHeader header;
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    
    if (header.version != FORMAT_VERSION) {
        return nullptr; // Version mismatch
    }
    
    // Deserialize tree
    return deserializeNode(file);
}

std::vector<uint8_t> Serializer::serializeStrategy(const Strategy& strategy) {
    std::vector<uint8_t> data;
    size_t num_actions = strategy.probabilities.size();
    data.resize(sizeof(uint32_t) + num_actions * sizeof(float));
    
    uint32_t* size_ptr = reinterpret_cast<uint32_t*>(data.data());
    *size_ptr = static_cast<uint32_t>(num_actions);
    
    float* probs_ptr = reinterpret_cast<float*>(data.data() + sizeof(uint32_t));
    for (size_t i = 0; i < num_actions; ++i) {
        probs_ptr[i] = strategy.probabilities[i];
    }
    
    return data;
}

Strategy Serializer::deserializeStrategy(const std::vector<uint8_t>& data) {
    if (data.size() < sizeof(uint32_t)) return Strategy();
    
    const uint32_t* size_ptr = reinterpret_cast<const uint32_t*>(data.data());
    uint32_t num_actions = *size_ptr;
    
    Strategy strategy(num_actions);
    const float* probs_ptr = reinterpret_cast<const float*>(data.data() + sizeof(uint32_t));
    for (uint32_t i = 0; i < num_actions && i < strategy.probabilities.size(); ++i) {
        strategy.probabilities[i] = probs_ptr[i];
    }
    
    return strategy;
}

void Serializer::serializeNode(std::ofstream& file, const std::shared_ptr<GameTreeNode>& node) {
    // Simplified serialization
    if (!node) return;
    
    // Write node type
    int32_t type = static_cast<int32_t>(node->type);
    file.write(reinterpret_cast<const char*>(&type), sizeof(type));
    
    // Write other node data...
}

std::shared_ptr<GameTreeNode> Serializer::deserializeNode(std::ifstream& file) {
    // Simplified deserialization
    int32_t type;
    file.read(reinterpret_cast<char*>(&type), sizeof(type));
    
    if (file.eof()) return nullptr;
    
    auto node = std::make_shared<GameTreeNode>(static_cast<GameTreeNode::Type>(type));
    return node;
}

} // namespace poker
