#include <Geode/Geode.hpp>
#include <Geode/modify/CCTextInputNode.hpp>

using namespace geode::prelude;

class $modify(UnlimitedTextInput, CCTextInputNode) {
    void setMaxLabelLength(int length) {
        CCTextInputNode::setMaxLabelLength(2147483647);
    }
};