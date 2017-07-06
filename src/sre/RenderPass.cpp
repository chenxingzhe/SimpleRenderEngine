//
// Created by Morten Nobel-Jørgensen on 08/06/2017.
//

#include "sre/RenderPass.hpp"
#include "sre/Mesh.hpp"
#include "sre/Shader.hpp"
#include "sre/Material.hpp"
#include "sre/RenderStats.hpp"
#include "sre/Texture.hpp"
#include "sre/impl/GL.hpp"
#include <cassert>
#include <glm/gtc/type_ptr.hpp>
#include <sre/imgui_sre.hpp>
#include <sre/Renderer.hpp>
#include <imgui_internal.h>

namespace sre {
    RenderPass * RenderPass::instance = nullptr;

    RenderPass::RenderPassBuilder::RenderPassBuilder(RenderStats* renderStats)
    :renderStats(renderStats)
    {
    }

    RenderPass::RenderPassBuilder& RenderPass::RenderPassBuilder::withName(const std::string& name){
        this->name = name;
        return *this;
    }

    RenderPass::RenderPassBuilder& RenderPass::RenderPassBuilder::withCamera(const Camera& camera){
        this->camera = camera;
        return *this;
    }

    RenderPass::RenderPassBuilder& RenderPass::RenderPassBuilder::withWorldLights(WorldLights* worldLights){
        this->worldLights = worldLights;
        return *this;
    }

    RenderPass::RenderPassBuilder &RenderPass::RenderPassBuilder::withClearColor(bool enabled, glm::vec4 color) {
        this->clearColor = enabled;
        this->clearColorValue = color;
        return *this;
    }

    RenderPass::RenderPassBuilder &RenderPass::RenderPassBuilder::withClearDepth(bool enabled, float value) {
        this->clearDepth = enabled;
        this->clearDepthValue = value;
        return *this;
    }

    RenderPass::RenderPassBuilder &RenderPass::RenderPassBuilder::withClearStencil(bool enabled, int value) {
        this->clearStencil = enabled;
        this->clearStencilValue = value;
        return *this;
    }

    RenderPass::RenderPassBuilder &RenderPass::RenderPassBuilder::withGUI(bool enabled) {
        this->gui = enabled;
        return *this;
    }

    RenderPass RenderPass::RenderPassBuilder::build(){
        GLbitfield clear = 0;
        if (clearColor) {
            glClearColor(clearColorValue.r, clearColorValue.g, clearColorValue.b, clearColorValue.a);
            clear |= GL_COLOR_BUFFER_BIT;
        }
        if (clearDepth) {
            glClearDepthf(clearDepthValue);
            glDepthMask(GL_TRUE);
            clear |= GL_DEPTH_BUFFER_BIT;
        }
        if (clearStencil) {
            glClearStencil(clearStencilValue);
            clear |= GL_STENCIL_BUFFER_BIT;
        }
        if (clear){
            glClear(clear);
        }

        if (this->gui){
            ImGui_SRE_NewFrame(Renderer::instance->window);
        }

        return RenderPass(std::move(camera), worldLights, renderStats, gui);
    }

    RenderPass::~RenderPass(){

    }

    RenderPass::RenderPass(Camera &&camera, WorldLights *worldLights, RenderStats *renderStats, bool gui)
        :camera(camera), worldLights(worldLights),renderStats(renderStats), gui(gui)
    {
        if (instance){
            instance->finish();
        }
        glViewport(camera.viewportX, camera.viewportY, camera.viewportWidth, camera.viewportHeight);
        glScissor(camera.viewportX, camera.viewportY, camera.viewportWidth, camera.viewportHeight);
        instance = this;
    }

    void RenderPass::draw(std::shared_ptr<Mesh>& meshPtr, glm::mat4 modelTransform, Material *material) {
        assert(instance != nullptr);

        Mesh* mesh = meshPtr.get();
        auto shader = material->getShader().get();
        assert(mesh  != nullptr);
        renderStats->drawCalls++;
        setupShader(modelTransform, shader);
        if (material != lastBoundMaterial){
            renderStats->stateChangesMaterial++;
            lastBoundMaterial = material;
            material->bind();
        }
        if (mesh != lastBoundMesh){
            renderStats->stateChangesMesh++;
            lastBoundMesh = mesh;
            mesh->bind(shader);
        }

        int indexCount = (int) mesh->getIndices().size();
        if (indexCount == 0){
            glDrawArrays((GLenum) mesh->getMeshTopology(), 0, mesh->getVertexCount());
        } else {
            glDrawElements((GLenum) mesh->getMeshTopology(), indexCount, GL_UNSIGNED_SHORT, 0);
        }
    }

    void RenderPass::setupShader(const glm::mat4 &modelTransform, Shader *shader)  {
        if (lastBoundShader == shader){
            if (shader->uniformLocationModel != -1){
                glUniformMatrix4fv(shader->uniformLocationModel, 1, GL_FALSE, glm::value_ptr(modelTransform));
            }
            if (shader->uniformLocationNormal != -1){
                auto normalMatrix = transpose(inverse((glm::mat3)(camera.getViewTransform() * modelTransform)));
                glUniformMatrix4fv(shader->uniformLocationNormal, 1, GL_FALSE, glm::value_ptr(normalMatrix));
            }
        } else {
            renderStats->stateChangesShader++;
            lastBoundShader = shader;
            shader->bind();
            if (shader->uniformLocationModel != -1){
                glUniformMatrix4fv(shader->uniformLocationModel, 1, GL_FALSE, glm::value_ptr(modelTransform));
            }
            if (shader->uniformLocationView != -1){
                glUniformMatrix4fv(shader->uniformLocationView, 1, GL_FALSE, glm::value_ptr(camera.viewTransform));
            }
            if (shader->uniformLocationProjection != -1){
                glUniformMatrix4fv(shader->uniformLocationProjection, 1, GL_FALSE, glm::value_ptr(camera.projectionTransform));
            }
            if (shader->uniformLocationNormal != -1){
                auto normalMatrix = transpose(inverse(((glm::mat3)camera.getViewTransform()) * ((glm::mat3)modelTransform)));
                glUniformMatrix3fv(shader->uniformLocationNormal, 1, GL_FALSE, glm::value_ptr(normalMatrix));
            }
            if (shader->uniformLocationViewport != -1){
                glm::vec4 viewport((float)camera.viewportWidth,(float)camera.viewportHeight,(float)camera.viewportX,(float)camera.viewportY);
                glUniform4fv(shader->uniformLocationViewport, 1, glm::value_ptr(viewport));
            }
            shader->setLights(worldLights, camera.getViewTransform());
        }
    }


    void RenderPass::drawLines(const std::vector<glm::vec3> &verts, glm::vec4 color, MeshTopology meshTopology) {
        assert(instance != nullptr);

        // Keep a shared mesh and material
        static Material material{Shader::getUnlit()};
        static std::shared_ptr<Mesh> mesh = Mesh::create()
                .withPositions(verts)
                .withMeshTopology(meshTopology)
                .build();

        // update shared mesh
        mesh->update().withPositions(verts).build();

        // update material
        material.setColor(color);
        draw(mesh, glm::mat4(1), &material);
    }

    void RenderPass::finish() {
        assert(instance != nullptr);
        if (gui){
            ImGui::Render();
        }
        instance = nullptr;
    }
}