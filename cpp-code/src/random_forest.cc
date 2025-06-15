#include "random_forest.hpp"

#include <fstream>
#include <stdexcept> // Required for std::runtime_error
#include <iostream>  // Required for std::cerr (or use a proper logger if available)

void RandomForestRegressor::loadModel(const std::string& model_file) {
  std::ifstream file(model_file, std::ios::binary);
  if (!file.is_open()) {
    throw std::runtime_error("Cannot open model file: " + model_file);
  }

  // Read number of trees
  uint32_t n_trees_uint;
  file.read(reinterpret_cast<char*>(&n_trees_uint), sizeof(n_trees_uint));
  if (!file) {
    throw std::runtime_error("Error reading n_trees from model file: " + model_file);
  }
  n_trees = static_cast<int>(n_trees_uint);
  std::cerr << "Info: RandomForestRegressor::loadModel - Read n_trees = " << n_trees << " from " << model_file << std::endl;


  if (n_trees <= 0) { // Check for invalid number of trees
    throw std::runtime_error("Invalid number of trees (<=0) in model file: " + model_file + ", n_trees_read: " + std::to_string(n_trees));
  }

  trees.resize(n_trees);

  for (int t = 0; t < n_trees; ++t) {
    // Read tree size
    uint32_t tree_size;
    file.read(reinterpret_cast<char*>(&tree_size), sizeof(tree_size));
    if (!file) {
      throw std::runtime_error("Error reading tree_size for tree " + std::to_string(t) + " from model file: " + model_file);
    }
    if (tree_size == 0) {
        throw std::runtime_error("Error: tree " + std::to_string(t) + " has zero size in model file: " + model_file);
    }

    trees[t].resize(tree_size);

    // Read nodes
    for (uint32_t i = 0; i < tree_size; ++i) {
      auto& node = trees[t][i];
      file.read(reinterpret_cast<char*>(&node.feature_idx), sizeof(node.feature_idx));
      if (!file) throw std::runtime_error("Error reading node.feature_idx for tree " + std::to_string(t) + ", node " + std::to_string(i));
      file.read(reinterpret_cast<char*>(&node.threshold), sizeof(node.threshold));
      if (!file) throw std::runtime_error("Error reading node.threshold for tree " + std::to_string(t) + ", node " + std::to_string(i));
      file.read(reinterpret_cast<char*>(&node.value), sizeof(node.value));
      if (!file) throw std::runtime_error("Error reading node.value for tree " + std::to_string(t) + ", node " + std::to_string(i));
      file.read(reinterpret_cast<char*>(&node.left_child), sizeof(node.left_child));
      if (!file) throw std::runtime_error("Error reading node.left_child for tree " + std::to_string(t) + ", node " + std::to_string(i));
      file.read(reinterpret_cast<char*>(&node.right_child), sizeof(node.right_child));
      if (!file) throw std::runtime_error("Error reading node.right_child for tree " + std::to_string(t) + ", node " + std::to_string(i));
      file.read(reinterpret_cast<char*>(&node.is_leaf), sizeof(node.is_leaf));
      if (!file) throw std::runtime_error("Error reading node.is_leaf for tree " + std::to_string(t) + ", node " + std::to_string(i));
    }
  }
  std::cerr << "Info: RandomForestRegressor::loadModel - Successfully loaded " << n_trees << " trees from " << model_file << std::endl;
  file.close();
}