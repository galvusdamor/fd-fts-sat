#ifndef TASK_TRANSFORMATION_LABEL_MAP_H
#define TASK_TRANSFORMATION_LABEL_MAP_H

#include <vector>
#include <iostream>

namespace task_transformation {


class LabelMapping {
    std::vector<int> old_to_new_labels;
    int num_new_labels;

public: 
LabelMapping(std::vector<int> old_to_new_labels, int num_new_labels) :
    old_to_new_labels(old_to_new_labels), num_new_labels(num_new_labels){ 
    }

    const std::vector<int> & get_old_to_new_labels() const {
        return old_to_new_labels;
    }

    int operator[](std::size_t var_id) const {
        return old_to_new_labels[var_id];
    }

    int operator[](int var) const {
        return old_to_new_labels[var];
    }

    int get_num_new_labels() const {
        return num_new_labels;
    }        
};

class LabelMap{
    std::vector<int> reduced_labels; // reduced label numbers indexed by original label number
//    std::vector<int> original_labels; // indexed by reduced label number
public:
    explicit LabelMap(int num_labels);
    LabelMap(const LabelMap & ) = default;
    
    void update(const LabelMapping &old_to_new_labels);
    void update(const std::vector<int> &old_to_new_labels);

    int get_reduced_label(int old_label) const{
        return reduced_labels[old_label];
    }

    int get_num_old_labels() const {
        return reduced_labels.size();
    }

    void dump() const;

//    int get_original_label(int reduced_label) const{
//        return original_labels[reduced_label];
//    }

        friend std::ostream &operator<<(std::ostream &os, const LabelMap & label_map);
};
}

#endif
