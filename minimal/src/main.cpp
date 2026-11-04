#include <Geode/Geode.hpp>
#include <Geode/modify/CCTextInputNode.hpp>
#include <climits>

using namespace geode::prelude;

class $modify(UnlimitedTextInput, CCTextInputNode) {
    void setMaxLabelLength(int length) {
        CCTextInputNode::setMaxLabelLength(INT_MAX);
    }
};