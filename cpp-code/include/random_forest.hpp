#pragma once
#include <cmath>
#include <cstdio> // For fprintf, if not already included by iostream/other headers indirectly
#include <limits>
#include <string>
#include <vector>

// Simple Random Forest implementation for inference only
struct DecisionNode {
  int feature_idx = -1;
  double threshold = 0.0;
  double value = 0.0;  // leaf value
  int left_child = -1;
  int right_child = -1;
  bool is_leaf = false;
};

class RandomForestRegressor {
 private:
  std::vector<std::vector<DecisionNode>> trees;
  std::vector<std::string> feature_names;
  int n_trees = 0;

 public:
  // Load model from exported coefficients
  void loadModel(const std::string& model_file);

  double predict(const std::vector<double>& features) const {
    if (n_trees == 0) {
      fprintf(stderr, "CRITICAL ERROR: RandomForestRegressor::predict called when n_trees = 0. Model was not loaded correctly or the model file is empty/corrupt. Returning NaN.\n");
      return std::numeric_limits<double>::quiet_NaN(); // Or return 0.0;
    }
    double sum = 0.0;
    for (const auto& tree : trees) {
      sum += predictTree(tree, features);
    }
    return sum / n_trees;
  }

  std::vector<double> predictBatch(
      const std::vector<std::vector<double>>& feature_batch) const {
    std::vector<double> predictions;
    predictions.reserve(feature_batch.size());
    for (const auto& features : feature_batch) {
      predictions.push_back(predict(features));
    }
    return predictions;
  }

 private:
  double predictTree(const std::vector<DecisionNode>& tree,
                     const std::vector<double>& features) const {
    if (tree.empty()) {
      fprintf(stderr,
              "CRITICAL ERROR: RandomForestRegressor::predictTree - Tree is "
              "empty.\n");
      return std::numeric_limits<double>::quiet_NaN();
    }
    int node_idx = 0;
    while (true) {
      // Boundary check for node_idx
      if (node_idx < 0 || static_cast<size_t>(node_idx) >= tree.size()) {
        fprintf(stderr,
                "CRITICAL ERROR: RandomForestRegressor::predictTree - current "
                "node_idx %d is out of bounds for tree size %zu. Tree or model "
                "is corrupt.\n",
                node_idx, tree.size());
        return std::numeric_limits<double>::quiet_NaN();
      }
      const auto& node = tree[node_idx];

      if (node.is_leaf) {
        if (std::isnan(node.value)) {
          // This means the model itself contains NaN leaf values from training
          fprintf(stderr,
                  "WARNING: RandomForestRegressor::predictTree - Leaf node %d "
                  "has NaN value. Model training issue?\n",
                  node_idx);
        }
        return node.value;
      }

      // Boundary check for feature_idx
      if (node.feature_idx < 0 ||
          static_cast<size_t>(node.feature_idx) >= features.size()) {
        fprintf(
            stderr,
            "CRITICAL ERROR: RandomForestRegressor::predictTree - "
            "node.feature_idx %d is out of bounds for features vector size %zu "
            "for node %d. Tree or model is corrupt or feature mismatch.\n",
            node.feature_idx, features.size(), node_idx);
        return std::numeric_limits<double>::quiet_NaN();
      }

      double feature_val = features[node.feature_idx];
      if (std::isnan(feature_val)) {
        // If a feature is NaN, the behavior of (NaN <= threshold) is false.
        // This means it will likely always go to the right child if the
        // condition is feature <= threshold. Or, you might decide to handle NaN
        // features explicitly (e.g., always go left, or a specific path).
        fprintf(stderr,
                "WARNING: RandomForestRegressor::predictTree - Feature %d is "
                "NaN at node %d. Prediction might be unreliable.\n",
                node.feature_idx, node_idx);
        // Defaulting to right child on NaN, or could return NaN immediately.
        // For now, let it proceed, but this is a source of instability.
        // return std::numeric_limits<double>::quiet_NaN(); // Option: Propagate
        // NaN immediately
      }

      if (feature_val <= node.threshold) {
        node_idx = node.left_child;
      } else {
        node_idx = node.right_child;
      }
    }
  }
};