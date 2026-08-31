import renderdoc as rd
import rdtest


class VK_Descriptor_Heap(rdtest.TestCase):
    demos_test_name = 'VK_Descriptor_Heap'

    def gather_draws(self, actions):
        for action in actions:
            if action.flags & (rd.ActionFlags.Drawcall | rd.ActionFlags.Dispatch):
                yield action
            yield from self.gather_draws(action.children)

    def check_capture(self):
        stage = rd.ShaderStage.Pixel

        for draw in self.gather_draws(self.find_action("Heap Tests").children):
            testName = draw.parent.customName
            rdtest.log.begin_section(f"Test {testName}")

            self.controller.SetFrameEvent(draw.eventId, False)

            pipe = self.controller.GetPipelineState()
            vkpipe = self.controller.GetVulkanPipelineState()
            refl = pipe.GetShaderReflection(stage)

            if (len(vkpipe.graphics.descriptorHeaps) != 2):
                raise rdtest.TestFailureException("Expected 2 heaps")
            # XXX: check that the heaps are resource and sampler.

            if not (draw.flags & rd.ActionFlags.Dispatch):
                self.check_postvs(testName, draw)

            res_data = [
                (pipe.GetReadOnlyResources(stage, True), refl.readOnlyResources),
                (pipe.GetReadWriteResources(stage, True), refl.readWriteResources),
                (pipe.GetConstantBlocks(stage, True), refl.constantBlocks),
            ]

            for reslist, refllist in res_data:
                for res in reslist:
                    refl_entry = refllist[res.access.index]
                    expected_name = refl_entry.name

                    # These accesses map to the command buffer, not one of our
                    # named resources
                    if expected_name in ["push", "descbuf"]:
                        continue

                    if refl_entry.bindArraySize > 1:
                        element = res.access.arrayElement
                        expected_name = f"{expected_name}_{res.access.arrayElement}"
                    else:
                        if res.access.arrayElement != 0:
                            raise rdtest.TestFailureException(
                                f"found unexpected array element {res.access.arrayElement}")

                    resource = self.get_resource(res.descriptor.resource)
                    if not resource:
                        raise rdtest.TestFailureException(
                            f"missing resource for access of {expected_name}")
                    resname = resource.name

                    if resname != expected_name:
                        raise rdtest.TestFailureException(
                            f"Expected resource {resname} to be named {expected_name}")

                    if hasattr(refl_entry, 'descriptorType'):
                        if res.descriptor.type != refl_entry.descriptorType:
                            raise rdtest.TestFailureException(
                                f"expected access of {r.descriptorType.name} to be {refl_entry.descriptorType} not {res.descriptor.type}")

                    if refl_entry.bindArraySize == 1:
                        usages = self.controller.GetUsage(
                            res.descriptor.resource)
                        if draw.eventId not in [usage.eventId for usage in usages]:
                            raise rdtest.TestFailureException(
                                f"expected GetUsage({expected_name}) to have EID {draw.eventId}")

                accessIndices = [res.access.index for res in reslist]
                for i, r in enumerate(refllist):
                    if i not in accessIndices:
                        raise rdtest.TestFailureException(
                            f"failed to find an access of {r.descriptorType.name} {r.name}")

            rdtest.log.success(
                "Dynamic usage is as expected for {}".format(testName))

            v = pipe.GetViewport(0)

            x = int(v.x) + int(v.width / 2)
            y = int(v.y) + int(v.height // 2)
            offset = int(v.height // 5)
            if refl.debugInfo.debuggable:
                self.check_debug_pixel(x, y)
                self.check_debug_pixel(x, y+offset)
                self.check_debug_pixel(x, y-offset)
            else:
                rdtest.log.comment(f'Shader is not debuggable, not verifying')

            rdtest.log.end_section(f"Test {testName}")

    def check_postvs(self, testName, action):
        postvs_data = self.get_postvs(
            action, rd.MeshDataStage.VSOut, 0, action.numIndices)

        postvs_ref = {
            0: {
                'vtx': 0,
                'idx': 0,
                'gl_Position': [-1.0, 1.0, 0.0, 1.0],
            },
            1: {
                'vtx': 1,
                'idx': 1,
                'gl_Position': [1.0, 1.0, 0.0, 1.0],
            },
            2: {
                'vtx': 2,
                'idx': 2,
                'gl_Position': [-1.0, -1.0, 0.0, 1.0],
            },
            3: {
                'vtx': 3,
                'idx': 3,
                'gl_Position': [1.0, -1.0, 0.0, 1.0],
            },
        }

        self.check_mesh_data(postvs_ref, postvs_data)
        if "GS" in testName:
            print("XXX: do geometry")
