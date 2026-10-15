#ifndef TASK_TRANSFORMATION_MERGE_AND_SHRINK_REPRESENTATION_H
#define TASK_TRANSFORMATION_MERGE_AND_SHRINK_REPRESENTATION_H

#include <memory>
#include <vector>

namespace task_representation {
class State;
}
class GlobalState;
using namespace task_representation;

namespace task_transformation {
class Distances;
class MergeAndShrinkRepresentation {
protected:
    int domain_size;

public:
    explicit MergeAndShrinkRepresentation(int domain_size);
    virtual ~MergeAndShrinkRepresentation() = 0;

    // Store distances instead of abstract state numbers.
    virtual void set_distances(const Distances &) = 0;
    int get_domain_size() const;

    // Return the abstract state or the goal distance, depending on whether
    // set_distances has been used or not.
    virtual int get_value(const std::vector<int> &state) const = 0;
    virtual int get_value(const State &state) const = 0;
    virtual int get_value(const GlobalState &state) const = 0;
    virtual void apply_abstraction_to_lookup_table(
        const std::vector<int> &abstraction_mapping) = 0;
    virtual void dump() const = 0;
};


class MergeAndShrinkRepresentationLeaf : public MergeAndShrinkRepresentation {
    const int var_id;

    std::vector<int> lookup_table;
public:
    MergeAndShrinkRepresentationLeaf(int var_id, int domain_size);
    virtual ~MergeAndShrinkRepresentationLeaf() = default;

    virtual void set_distances(const Distances &) override;
    virtual void apply_abstraction_to_lookup_table(
        const std::vector<int> &abstraction_mapping) override;
    virtual int get_value(const std::vector<int> &state) const override;
    
    virtual int get_value(const State &state) const override;
    virtual int get_value(const GlobalState &state) const override;
    virtual void dump() const override;

    int get_var_id() const {return var_id;}
    const std::vector<int> &get_lookup_table() const {return lookup_table;}
};


class MergeAndShrinkRepresentationMerge : public MergeAndShrinkRepresentation {
    std::unique_ptr<MergeAndShrinkRepresentation> left_child;
    std::unique_ptr<MergeAndShrinkRepresentation> right_child;
    std::vector<std::vector<int>> lookup_table;
public:
    MergeAndShrinkRepresentationMerge(
        std::unique_ptr<MergeAndShrinkRepresentation> left_child,
        std::unique_ptr<MergeAndShrinkRepresentation> right_child);
    virtual ~MergeAndShrinkRepresentationMerge() = default;

    virtual void set_distances(const Distances &distances) override;
    virtual void apply_abstraction_to_lookup_table(
        const std::vector<int> &abstraction_mapping) override;
    virtual int get_value(const std::vector<int> &state) const override;
    virtual int get_value(const State &state) const override;
    virtual int get_value(const GlobalState &state) const override;
    virtual void dump() const override;
};
}

#endif
