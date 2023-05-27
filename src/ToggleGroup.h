///////////////////////////////////////////////////////////////////////
//
// Part of ShaderToggler, a shader toggler add on for Reshade 5+ which allows you
// to define groups of shaders to toggle them on/off with one key press
// 
// (c) Frans 'Otis_Inf' Bouma.
//
// All rights reserved.
// https://github.com/FransBouma/ShaderToggler
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met :
//
//  * Redistributions of source code must retain the above copyright notice, this
//	  list of conditions and the following disclaimer.
//
//  * Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and / or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED.IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
/////////////////////////////////////////////////////////////////////////
#pragma once

#include <string>
#include <unordered_set>
#include <unordered_map>

#include "CDataFile.h"

namespace ShaderToggler
{
    class ToggleGroup
    {
    public:
        ToggleGroup(std::string name, int Id);
        ToggleGroup();

        static int getNewGroupId();

        void setToggleKey(uint32_t keybind) { _keybind = keybind; }
        void setName(std::string newName);
        /// <summary>
        /// Writes the shader hashes, name and toggle key to the ini file specified, using a Group + groupCounter section.
        /// </summary>
        /// <param name="iniFile"></param>
        /// <param name="groupCounter"></param>
        void saveState(CDataFile& iniFile, int groupCounter) const;
        /// <summary>
        /// Loads the shader hashes, name and toggle key from the ini file specified, using a Group + groupCounter section.
        /// </summary>
        /// <param name="iniFile"></param>
        /// <param name="groupCounter">if -1, the ini file is in the pre-1.0 format</param>
        void loadState(CDataFile& iniFile, int groupCounter);
        void storeCollectedHashes(const std::unordered_set<uint32_t> pixelShaderHashes, const std::unordered_set<uint32_t> vertexShaderHashes);
        bool isBlockedVertexShader(uint32_t shaderHash) const;
        bool isBlockedPixelShader(uint32_t shaderHash) const;
        void clearHashes();

        void toggleActive() { _isActive = !_isActive; }
        void setEditing(bool isEditing) { _isEditing = isEditing; }

        uint32_t getToggleKey() { return _keybind; }
        std::string getName() { return _name; }
        bool isActive() const { return _isActive; }
        bool isEditing() { return _isEditing; }
        bool isEmpty() const { return _vertexShaderHashes.size() <= 0 && _pixelShaderHashes.size() <= 0; }
        int getId() const { return _id; }
        const std::unordered_set<std::string>& preferredTechniques() const { return _preferredTechniques; }
        void setPreferredTechniques(std::unordered_set<std::string> techniques) { _preferredTechniques = techniques; }
        std::unordered_set<uint32_t> getPixelShaderHashes() const { return _pixelShaderHashes; }
        std::unordered_set<uint32_t> getVertexShaderHashes() const { return _vertexShaderHashes; }
        void setInvocationLocation(uint32_t location) { _invocationLocation = location; }
        uint32_t getInvocationLocation() const { return _invocationLocation; }
        void setSlotIndex(uint32_t index) { _slotIndex = index; }
        uint32_t getSlotIndex() const { return _slotIndex; }
        void setDescriptorIndex(uint32_t index) { _descIndex = index; }
        uint32_t getDescriptorIndex() const { return _descIndex; }
        bool isProvidingTextureBinding() const { return _isProvidingTextureBinding; }
        void setProvidingTextureBinding(bool isProvidingTextureBinding) { _isProvidingTextureBinding = isProvidingTextureBinding; }
        const std::string& getTextureBindingName() const { return _textureBindingName; }
        void setTextureBindingName(std::string textureBindingName) { _textureBindingName = textureBindingName; }
        bool getClearBindings() { return _clearBindings; }
        void setClearBindings(bool clear) { _clearBindings = clear; }
        bool getAllowAllTechniques() const { return _allowAllTechniques; }
        void setAllowAllTechniques(bool allowAllTechniques) { _allowAllTechniques = allowAllTechniques; }
        bool getExtractConstants() const { return _extractConstants; }
        void setExtractConstant(bool extract) { _extractConstants = extract; }
        bool getExtractResourceViews() const { return _extractResourceViews; }
        void setExtractResourceViews(bool extract) { _extractResourceViews = extract; }
        void setSRVSlotIndex(uint32_t index) { _srvSlotIndex = index; }
        uint32_t getSRVSlotIndex() const { return _srvSlotIndex; }
        void setSRVDescriptorIndex(uint32_t index) { _srvDescIndex = index; }
        uint32_t getSRVDescriptorIndex() const { return _srvDescIndex; }
        bool getHasTechniqueExceptions() const { return _hasTechniqueExceptions; }
        void setHasTechniqueExceptions(bool exceptions) { _hasTechniqueExceptions = exceptions; }
        bool getMatchSwapchainResolution() const { return _matchSwapchainResolution; }
        void setMatchSwapchainResolution(bool match) { _matchSwapchainResolution = match; }
        bool getRequeueAfterRTMatchingFailure() const { return _requeueAfterRTMatchingFailure; }
        void setRequeueAfterRTMatchingFailure(bool requeue) { _requeueAfterRTMatchingFailure = requeue; }
        const std::unordered_map<std::string, std::tuple<uintptr_t, bool>>& GetVarOffsetMapping() const { return _varOffsetMapping; }
        bool SetVarMapping(uintptr_t, std::string&, bool);
        bool RemoveVarMapping(std::string&);

        bool operator==(const ToggleGroup& rhs)
        {
            return getId() == rhs.getId();
        }

    private:
        int _id;
        std::string	_name;
        uint32_t _keybind;
        std::unordered_set<uint32_t> _vertexShaderHashes;
        std::unordered_set<uint32_t> _pixelShaderHashes;
        uint32_t _invocationLocation;
        uint32_t _slotIndex = 0;
        uint32_t _descIndex = 0;
        uint32_t _srvSlotIndex = 0;
        uint32_t _srvDescIndex = 0;
        bool _isActive;				// true means the group is actively toggled (so the hashes have to be hidden.
        bool _isEditing;			// true means the group is actively edited (name, key)
        bool _allowAllTechniques;	// true means all techniques are allowed, regardless of preferred techniques.
        bool _isProvidingTextureBinding;
        bool _extractConstants;
        bool _extractResourceViews;
        bool _clearBindings;
        bool _hasTechniqueExceptions; // _preferredTechniques are handled as exception to _allowAllTechniques
        bool _matchSwapchainResolution;
        bool _requeueAfterRTMatchingFailure;
        std::string _textureBindingName;
        std::unordered_set<std::string> _preferredTechniques;
        std::unordered_map<std::string, std::tuple<uintptr_t, bool>> _varOffsetMapping;
    };
}
