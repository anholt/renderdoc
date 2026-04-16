import renderdoc as rd
import rdtest

class VK_Resource_Usage(rdtest.TestCase):
    demos_test_name = 'VK_Resource_Usage'
    resourceUsages = {}

    def check_resource_usage(self, res: rd.ResourceDescription, expectedUsages=[]):
        usages = self.resourceUsages[res.resourceId]
        if len(usages) != len(expectedUsages):
            for u in usages:
                rdtest.log.print(f"Resource '{res.name}' {res.resourceId} usage EID:{u.eventId} usage:{u.usage.name}")
            raise rdtest.TestFailureException(f"'{res.name}' {res.resourceId} Incorrect resource usages count expected:{len(expectedUsages)} actual:{len(usages)}")
        for i, u in enumerate(usages):
            eid, usage = expectedUsages[i]
            if u.usage != usage:
                raise rdtest.TestFailureException(f"'{res.name}' {res.resourceId} EID:{u.eventId} Incorrect resource usage expected:{usage.name} actual:{u.usage.name}")
            if u.eventId != eid:
                raise rdtest.TestFailureException(f"'{res.name}' {res.resourceId} usage:{u.usage.name} Incorrect resource usage EID expected:{eid} actual:{u.eventId}")

    def check_capture(self):
        # Cache the resource usage before running any replay i.e. without calling SetFrameEvent
        resources = self.controller.GetResources()
        for res in resources:
            self.resourceUsages[res.resourceId] = self.controller.GetUsage(res.resourceId)

        drawIndirectCount = self.find_action("Draw Indirect Count") is not None
        rdtest.log.print(f"Has Draw Indirect Count: {'Yes' if drawIndirectCount else 'No'}")

        nestedSecondaries = self.find_action("Nested Secondary Command Buffer") is not None
        rdtest.log.print(f"Has Nested Secondary Command Buffer: {'Yes' if nestedSecondaries else 'No'}")

        descBuffer = self.find_action("Descriptor Buffer") is not None
        rdtest.log.print(f"Has Descriptor Buffer: {'Yes' if descBuffer else 'No'}")

        countDrawIndirectCount = 30 if drawIndirectCount else 0
        countNested = 39 if nestedSecondaries else 0
        countDescBuffer = 21 if descBuffer else 0

        action = self.find_action("Draw")
        self.controller.SetFrameEvent(action.eventId, False)
        swapImage = self.controller.GetPipelineState().GetOutputTargets()[0].resource

        for res in self.controller.GetResources():
            expectedUsage = []
            if res.type == rd.ResourceType.Device:
                expectedUsage = [(0,rd.ResourceUsage.Unused)]
            elif res.type == rd.ResourceType.Queue:
                expectedUsage = [(0,rd.ResourceUsage.Unused)]
            elif res.type == rd.ResourceType.Pool:
                expectedUsage = [(0,rd.ResourceUsage.Unused)]
            elif res.type == rd.ResourceType.SwapchainImage:
                # the swap chain image has usage, anything else does not
                if res.resourceId == swapImage:
                    expectedUsage = [(6,rd.ResourceUsage.Barrier), 
                                     (6,rd.ResourceUsage.Discard), 
                                     (7,rd.ResourceUsage.Clear), 
                                     (8,rd.ResourceUsage.Barrier), 
                                     (32,rd.ResourceUsage.ColorTarget), 
                                     (35,rd.ResourceUsage.ColorTarget), 
                                     (42,rd.ResourceUsage.ColorTarget), 
                                     (45,rd.ResourceUsage.ColorTarget), 
                                     (59,rd.ResourceUsage.ColorTarget), 
                                     (62,rd.ResourceUsage.ColorTarget), 
                                     (105,rd.ResourceUsage.ColorTarget), 
                                     (109,rd.ResourceUsage.ColorTarget), 
                                     (110,rd.ResourceUsage.ColorTarget), 
                                     (111,rd.ResourceUsage.ColorTarget), 
                                     (112,rd.ResourceUsage.ColorTarget), 
                                     (117,rd.ResourceUsage.ColorTarget), 
                                     (118,rd.ResourceUsage.ColorTarget), 
                                     (119,rd.ResourceUsage.ColorTarget), 
                                     (155,rd.ResourceUsage.ColorTarget), 
                                     (159,rd.ResourceUsage.ColorTarget), 
                                     (160,rd.ResourceUsage.ColorTarget), 
                                     (161,rd.ResourceUsage.ColorTarget), 
                                     (162,rd.ResourceUsage.ColorTarget), 
                                     (167,rd.ResourceUsage.ColorTarget), 
                                     (168,rd.ResourceUsage.ColorTarget), 
                                     (169,rd.ResourceUsage.ColorTarget), 
                                     (186,rd.ResourceUsage.ColorTarget), 
                                     (190,rd.ResourceUsage.ColorTarget), 
                                     (191,rd.ResourceUsage.ColorTarget), 
                                     (192,rd.ResourceUsage.ColorTarget), 
                                     (193,rd.ResourceUsage.ColorTarget), 
                                     (198,rd.ResourceUsage.ColorTarget), 
                                     (199,rd.ResourceUsage.ColorTarget), 
                                     (200,rd.ResourceUsage.ColorTarget)] 
                    if drawIndirectCount:
                        expectedUsage += [
                                     (221,rd.ResourceUsage.ColorTarget), 
                                     (222,rd.ResourceUsage.ColorTarget), 
                                     (223,rd.ResourceUsage.ColorTarget), 
                                     (228,rd.ResourceUsage.ColorTarget), 
                                     (229,rd.ResourceUsage.ColorTarget)]
                    if nestedSecondaries:
                        expectedUsage += [
                                     (224+countDrawIndirectCount,rd.ResourceUsage.ColorTarget), 
                                     (227+countDrawIndirectCount,rd.ResourceUsage.ColorTarget)] 
                    if descBuffer:
                        expectedUsage += [
                                     (219+countDrawIndirectCount+countNested,rd.ResourceUsage.ColorTarget), 
                                     (222+countDrawIndirectCount+countNested,rd.ResourceUsage.ColorTarget)] 

                    expectedUsage += [(208+countDrawIndirectCount+countNested+countDescBuffer,rd.ResourceUsage.Barrier)]
                else:
                    expectedUsage = []
            elif res.type == rd.ResourceType.RenderPass:
                expectedUsage = [(0,rd.ResourceUsage.Unused)]
            elif res.type == rd.ResourceType.Sync:
                expectedUsage = [(0,rd.ResourceUsage.Unused)]
            elif res.type == rd.ResourceType.View:
                expectedUsage = [(0,rd.ResourceUsage.Unused)]
            elif res.type == rd.ResourceType.Memory:
                expectedUsage = [(0,rd.ResourceUsage.Unused)]
            elif res.type == rd.ResourceType.ShaderBinding:
                expectedUsage = [(0,rd.ResourceUsage.Unused)]
            elif res.type == rd.ResourceType.Shader:
                expectedUsage = [(0,rd.ResourceUsage.Unused)]
            elif res.type == rd.ResourceType.PipelineState:
                expectedUsage = [(0,rd.ResourceUsage.Unused)]
            elif res.type == rd.ResourceType.Buffer:
                if (res.name == "Vertex Buffer"):
                    expectedUsage = [(32,rd.ResourceUsage.VertexBuffer), 
                                     (35,rd.ResourceUsage.VertexBuffer),
                                     (42,rd.ResourceUsage.VertexBuffer),
                                     (45,rd.ResourceUsage.VertexBuffer),
                                     (59,rd.ResourceUsage.VertexBuffer), 
                                     (62,rd.ResourceUsage.VertexBuffer), 
                                     (105,rd.ResourceUsage.VertexBuffer), 
                                     (109,rd.ResourceUsage.VertexBuffer), 
                                     (110,rd.ResourceUsage.VertexBuffer), 
                                     (111,rd.ResourceUsage.VertexBuffer), 
                                     (112,rd.ResourceUsage.VertexBuffer), 
                                     (117,rd.ResourceUsage.VertexBuffer), 
                                     (118,rd.ResourceUsage.VertexBuffer), 
                                     (119,rd.ResourceUsage.VertexBuffer), 
                                     (155,rd.ResourceUsage.VertexBuffer), 
                                     (159,rd.ResourceUsage.VertexBuffer), 
                                     (160,rd.ResourceUsage.VertexBuffer), 
                                     (161,rd.ResourceUsage.VertexBuffer), 
                                     (162,rd.ResourceUsage.VertexBuffer), 
                                     (167,rd.ResourceUsage.VertexBuffer), 
                                     (168,rd.ResourceUsage.VertexBuffer), 
                                     (169,rd.ResourceUsage.VertexBuffer), 
                                     (186,rd.ResourceUsage.VertexBuffer), 
                                     (190,rd.ResourceUsage.VertexBuffer), 
                                     (191,rd.ResourceUsage.VertexBuffer), 
                                     (192,rd.ResourceUsage.VertexBuffer), 
                                     (193,rd.ResourceUsage.VertexBuffer), 
                                     (198,rd.ResourceUsage.VertexBuffer), 
                                     (199,rd.ResourceUsage.VertexBuffer), 
                                     (200,rd.ResourceUsage.VertexBuffer)]
                    if drawIndirectCount:
                        expectedUsage += [
                                     (221,rd.ResourceUsage.VertexBuffer), 
                                     (222,rd.ResourceUsage.VertexBuffer), 
                                     (223,rd.ResourceUsage.VertexBuffer), 
                                     (228,rd.ResourceUsage.VertexBuffer), 
                                     (229,rd.ResourceUsage.VertexBuffer)]
                    if nestedSecondaries:
                        expectedUsage += [
                                     (224+countDrawIndirectCount,rd.ResourceUsage.VertexBuffer), 
                                     (227+countDrawIndirectCount,rd.ResourceUsage.VertexBuffer)]
                    if descBuffer:
                        expectedUsage += [
                                     (219+countDrawIndirectCount+countNested,rd.ResourceUsage.VertexBuffer), 
                                     (222+countDrawIndirectCount+countNested,rd.ResourceUsage.VertexBuffer)]
                if (res.name == "Index Buffer"):
                    expectedUsage = [(35,rd.ResourceUsage.IndexBuffer),
                                     (45,rd.ResourceUsage.IndexBuffer),
                                     (62,rd.ResourceUsage.IndexBuffer),
                                     (117,rd.ResourceUsage.IndexBuffer),
                                     (118,rd.ResourceUsage.IndexBuffer),
                                     (119,rd.ResourceUsage.IndexBuffer),
                                     (167,rd.ResourceUsage.IndexBuffer),
                                     (168,rd.ResourceUsage.IndexBuffer),
                                     (169,rd.ResourceUsage.IndexBuffer),
                                     (198,rd.ResourceUsage.IndexBuffer),
                                     (199,rd.ResourceUsage.IndexBuffer),
                                     (200,rd.ResourceUsage.IndexBuffer)]
                    if drawIndirectCount:
                        expectedUsage += [
                                     (228,rd.ResourceUsage.IndexBuffer),
                                     (229,rd.ResourceUsage.IndexBuffer)]
                    if nestedSecondaries:
                        expectedUsage += [
                                     (227+countDrawIndirectCount,rd.ResourceUsage.IndexBuffer)]
                    if descBuffer:
                        expectedUsage += [
                                     (222+countDrawIndirectCount+countNested,rd.ResourceUsage.IndexBuffer)]
                if (res.name == "Compute Buffer In"):
                    expectedUsage += [(73,rd.ResourceUsage.CS_Constants),
                                     (80,rd.ResourceUsage.CS_Constants)]
                    if nestedSecondaries:
                        expectedUsage += [(240+countDrawIndirectCount,rd.ResourceUsage.CS_Constants)]
                    if descBuffer:
                        expectedUsage += [(227+countDrawIndirectCount+countNested,rd.ResourceUsage.CS_Constants)]
                if (res.name == "Compute Buffer Out"):
                    expectedUsage += [(73,rd.ResourceUsage.CS_RWResource),
                                     (80,rd.ResourceUsage.CS_RWResource)]
                    if nestedSecondaries:
                        expectedUsage += [(240+countDrawIndirectCount,rd.ResourceUsage.CS_RWResource)]
                    if descBuffer:
                        expectedUsage += [(227+countDrawIndirectCount+countNested,rd.ResourceUsage.CS_RWResource)]
                if (res.name == "Indirect Data"):
                    expectedUsage += [(14,rd.ResourceUsage.Barrier),
                                     (15,rd.ResourceUsage.Clear),
                                     (16,rd.ResourceUsage.Barrier),
                                     (20,rd.ResourceUsage.CS_RWResource),
                                     (21,rd.ResourceUsage.Barrier),
                                     (81,rd.ResourceUsage.Barrier),
                                     (92,rd.ResourceUsage.CS_RWResource),
                                     (92,rd.ResourceUsage.Indirect),
                                     (93,rd.ResourceUsage.Barrier),
                                     (105,rd.ResourceUsage.Indirect),
                                     (108,rd.ResourceUsage.Indirect),
                                     (116,rd.ResourceUsage.Indirect),
                                     (122,rd.ResourceUsage.Barrier),
                                     (127,rd.ResourceUsage.Barrier),
                                     (128,rd.ResourceUsage.Clear),
                                     (129,rd.ResourceUsage.Barrier),
                                     (133,rd.ResourceUsage.CS_RWResource),
                                     (135,rd.ResourceUsage.Barrier),
                                     (137,rd.ResourceUsage.CS_RWResource),
                                     (137,rd.ResourceUsage.Indirect),
                                     (138,rd.ResourceUsage.CS_RWResource),
                                     (138,rd.ResourceUsage.Indirect),
                                     (139,rd.ResourceUsage.Barrier),
                                     (140,rd.ResourceUsage.CS_RWResource),
                                     (140,rd.ResourceUsage.Indirect),
                                     (141,rd.ResourceUsage.Barrier),
                                     (155,rd.ResourceUsage.Indirect),
                                     (158,rd.ResourceUsage.Indirect),
                                     (166,rd.ResourceUsage.Indirect),
                                     (186,rd.ResourceUsage.Indirect),
                                     (189,rd.ResourceUsage.Indirect),
                                     (197,rd.ResourceUsage.Indirect)]
                    if drawIndirectCount:
                        expectedUsage += [
                                     (205,rd.ResourceUsage.Indirect),
                                     (205,rd.ResourceUsage.Indirect),
                                     (208,rd.ResourceUsage.Indirect),
                                     (208,rd.ResourceUsage.Indirect),
                                     (212,rd.ResourceUsage.Indirect),
                                     (212,rd.ResourceUsage.Indirect),
                                     (216,rd.ResourceUsage.Indirect),
                                     (216,rd.ResourceUsage.Indirect),
                                     (220,rd.ResourceUsage.Indirect),
                                     (220,rd.ResourceUsage.Indirect),
                                     (227,rd.ResourceUsage.Indirect),
                                     (227,rd.ResourceUsage.Indirect)]
                    expectedUsage += [(204+countDrawIndirectCount,rd.ResourceUsage.Barrier)]
                    if nestedSecondaries:
                        expectedUsage += [
                                     (241+countDrawIndirectCount,rd.ResourceUsage.Barrier)]
            elif res.type == rd.ResourceType.Texture:
                if (res.name == "Offscreen MSAA Image"):
                    expectedUsage = [(11,rd.ResourceUsage.Barrier), 
                                     (11,rd.ResourceUsage.Discard), 
                                     (12,rd.ResourceUsage.Clear)]
                if (res.name == "Offscreen Image"):
                    expectedUsage = [(9,rd.ResourceUsage.Barrier), 
                                     (9,rd.ResourceUsage.Discard), 
                                     (10,rd.ResourceUsage.Clear), 
                                     (42,rd.ResourceUsage.PS_Resource), 
                                     (45,rd.ResourceUsage.PS_Resource), 
                                     (105,rd.ResourceUsage.PS_Resource), 
                                     (109,rd.ResourceUsage.PS_Resource), 
                                     (110,rd.ResourceUsage.PS_Resource), 
                                     (111,rd.ResourceUsage.PS_Resource), 
                                     (112,rd.ResourceUsage.PS_Resource), 
                                     (117,rd.ResourceUsage.PS_Resource), 
                                     (118,rd.ResourceUsage.PS_Resource), 
                                     (119,rd.ResourceUsage.PS_Resource), 
                                     (155,rd.ResourceUsage.PS_Resource), 
                                     (159,rd.ResourceUsage.PS_Resource), 
                                     (160,rd.ResourceUsage.PS_Resource), 
                                     (161,rd.ResourceUsage.PS_Resource), 
                                     (162,rd.ResourceUsage.PS_Resource), 
                                     (167,rd.ResourceUsage.PS_Resource), 
                                     (168,rd.ResourceUsage.PS_Resource), 
                                     (169,rd.ResourceUsage.PS_Resource), 
                                     (186,rd.ResourceUsage.PS_Resource), 
                                     (190,rd.ResourceUsage.PS_Resource), 
                                     (191,rd.ResourceUsage.PS_Resource), 
                                     (192,rd.ResourceUsage.PS_Resource), 
                                     (193,rd.ResourceUsage.PS_Resource), 
                                     (198,rd.ResourceUsage.PS_Resource), 
                                     (199,rd.ResourceUsage.PS_Resource), 
                                     (200,rd.ResourceUsage.PS_Resource)]
                    if drawIndirectCount:
                        expectedUsage += [
                                     (221,rd.ResourceUsage.PS_Resource), 
                                     (222,rd.ResourceUsage.PS_Resource), 
                                     (223,rd.ResourceUsage.PS_Resource), 
                                     (228,rd.ResourceUsage.PS_Resource), 
                                     (229,rd.ResourceUsage.PS_Resource)]
                    if descBuffer:
                        expectedUsage += [
                                     (219+countDrawIndirectCount+countNested,rd.ResourceUsage.PS_Resource), 
                                     (222+countDrawIndirectCount+countNested,rd.ResourceUsage.PS_Resource)]
            elif res.type == rd.ResourceType.CommandBuffer:
                expectedUsage = [(0,rd.ResourceUsage.Unused)]
            elif res.type == rd.ResourceType.DescriptorStore:
                expectedUsage = [(0,rd.ResourceUsage.Unused)]
            elif res.type == rd.ResourceType.Sampler:
                expectedUsage = [(0,rd.ResourceUsage.Unused)]
            else:
                raise rdtest.TestFailureException(f"'{res.name}' {res.resourceId} Unexpected resource type {res.type.name}")
            rdtest.log.print(f"Resource '{res.name}' type:{res.type.name} {res.resourceId} usages:{len(self.controller.GetUsage(res.resourceId))} expectedUsages:{len(expectedUsage)}")
            self.check_resource_usage(res, expectedUsage)

