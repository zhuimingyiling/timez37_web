export async function onRequestPost(context) {
    try {
        // 1. 获取前端传来的数据
        const { tableData, textData, prompt } = await context.request.json();
        
        // 2. 从 Cloudflare 环境变量中读取 API Key
        const API_KEY = context.env.API_KEY; 

        if (!API_KEY) {
            return new Response(JSON.stringify({ response: "错误：未在 Cloudflare 后台设置 API_KEY 变量。" }));
        }

        // 3. 整理发送给 AI 的数据上下文
        let dataContent = tableData ? JSON.stringify(tableData.slice(0, 100)) : (textData || "暂无数据内容");

        // 4. 使用 fetch 直接调用 Gemini HTTP API (无需导入 SDK)
        const response = await fetch(`https://generativelanguage.googleapis.com/v1beta/models/gemini-1.5-flash:generateContent?key=${API_KEY}`, {
            method: "POST",
            headers: {
                "Content-Type": "application/json"
            },
            body: JSON.stringify({
                contents: [{
                    parts: [{
                        text: `数据：${dataContent}\n\n问题：${prompt}`
                    }]
                }]
            })
        });

        const result = await response.json();

        // 5. 解析并返回 AI 的回答
        const aiMsg = result.candidates?.[0]?.content?.parts?.[0]?.text || "AI 忙碌中，请检查 API 状态。";
        
        return new Response(JSON.stringify({ response: aiMsg }));

    } catch (err) {
        return new Response(JSON.stringify({ response: "后端程序出错：" + err.message }));
    }
}
