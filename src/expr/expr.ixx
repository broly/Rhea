export module expr;

import <variant>;
import <string_view>;
import <locale>;
import <vector>;
import <unordered_map>;

export namespace expr
{
    
    
using Value = std::variant<bool, std::string_view>;

bool is_bool(const Value& v) {
    return std::holds_alternative<bool>(v);
}

bool as_bool(const Value& v) {
    return std::get<bool>(v);
}

enum class TokenType {
    Identifier,
    BoolLiteral,
    Operator,
    LParen,
    RParen,
    End
};

struct Token {
    TokenType type;
    std::string_view text;
};

struct Lexer {
    std::string_view src;
    size_t pos = 0;

    bool eof() const { return pos >= src.size(); }

    void skip_ws() {
        while (!eof() && std::isspace(src[pos])) pos++;
    }

    Token next() {
        skip_ws();
        if (eof()) return {TokenType::End, {}};

        char c = src[pos];

        if (std::isalpha(c) || c == '_') {
            size_t start = pos++;
            while (!eof() && (std::isalnum(src[pos]) || src[pos] == '_'))
                pos++;
            auto text = src.substr(start, pos - start);
            if (text == "true" || text == "false")
                return {TokenType::BoolLiteral, text};
            return {TokenType::Identifier, text};
        }

        if (c == '(') { pos++; return {TokenType::LParen, "("}; }
        if (c == ')') { pos++; return {TokenType::RParen, ")"}; }

        if (src.substr(pos, 2) == "||" ||
            src.substr(pos, 2) == "&&" ||
            src.substr(pos, 2) == "==" ||
            src.substr(pos, 2) == "!=") {
            auto t = src.substr(pos, 2);
            pos += 2;
            return {TokenType::Operator, t};
        }

        if (c == '!') {
            pos++;
            return {TokenType::Operator, "!"};
        }

        throw std::runtime_error("Unexpected character");
    }
};

enum class NodeType {
    Or, And, Not,
    Equal, NotEqual,
    Identifier,
    Literal
};

struct Node {
    NodeType type;
    int lhs = -1;
    int rhs = -1;
    Value literal{};
    std::string_view ident{};
};

struct Parser {
    std::vector<Token> tokens;
    size_t pos = 0;
    std::vector<Node> nodes;

    Token& peek() { return tokens[pos]; }
    Token& consume() { return tokens[pos++]; }

    int make(Node n) {
        nodes.push_back(n);
        return int(nodes.size() - 1);
    }

    int parse_expr() { return parse_or(); }

    int parse_or() {
        int lhs = parse_and();
        while (peek().type == TokenType::Operator && peek().text == "||") {
            consume();
            int rhs = parse_and();
            lhs = make({NodeType::Or, lhs, rhs});
        }
        return lhs;
    }

    int parse_and() {
        int lhs = parse_eq();
        while (peek().type == TokenType::Operator && peek().text == "&&") {
            consume();
            int rhs = parse_eq();
            lhs = make({NodeType::And, lhs, rhs});
        }
        return lhs;
    }

    int parse_eq() {
        int lhs = parse_unary();
        while (peek().type == TokenType::Operator &&
              (peek().text == "==" || peek().text == "!=")) {
            auto op = consume().text;
            int rhs = parse_unary();
            lhs = make({op == "==" ? NodeType::Equal : NodeType::NotEqual, lhs, rhs});
        }
        return lhs;
    }

    int parse_unary() {
        if (peek().type == TokenType::Operator && peek().text == "!") {
            consume();
            int v = parse_unary();
            return make({NodeType::Not, v});
        }
        return parse_primary();
    }

    int parse_primary() {
        Token t = consume();
        if (t.type == TokenType::Identifier)
            return make({NodeType::Identifier, -1, -1, {}, t.text});

        if (t.type == TokenType::BoolLiteral)
            return make({NodeType::Literal, -1, -1, t.text == "true"});

        if (t.type == TokenType::LParen) {
            int e = parse_expr();
            if (consume().type != TokenType::RParen)
                throw std::runtime_error("Expected ')'");
            return e;
        }

        throw std::runtime_error("Unexpected token");
    }
};


using Context = std::unordered_map<std::string_view, Value>;

bool eval_node(int id, const std::vector<Node>& nodes, const Context& ctx);

Value eval_value(int id, const std::vector<Node>& nodes, const Context& ctx) {
    const Node& n = nodes[id];
    if (n.type == NodeType::Literal)
        return n.literal;
    if (n.type == NodeType::Identifier)
        return ctx.at(n.ident);
    throw std::runtime_error("Invalid value node");
}

bool eval_node(int id, const std::vector<Node>& nodes, const Context& ctx) {
    const Node& n = nodes[id];
    switch (n.type) {
        case NodeType::Or:
            return eval_node(n.lhs, nodes, ctx) ||
                   eval_node(n.rhs, nodes, ctx);
        case NodeType::And:
            return eval_node(n.lhs, nodes, ctx) &&
                   eval_node(n.rhs, nodes, ctx);
        case NodeType::Not:
            return !eval_node(n.lhs, nodes, ctx);
        case NodeType::Equal:
            return eval_value(n.lhs, nodes, ctx) ==
                   eval_value(n.rhs, nodes, ctx);
        case NodeType::NotEqual:
            return eval_value(n.lhs, nodes, ctx) !=
                   eval_value(n.rhs, nodes, ctx);
        case NodeType::Identifier:
            return as_bool(ctx.at(n.ident));
        case NodeType::Literal:
            return as_bool(n.literal);
    }
    return false;
}


struct CompiledExpr {
    std::vector<Node> nodes;
    int root;
};

CompiledExpr compile(std::string_view expr) {
    Lexer lex{expr};
    std::vector<Token> tokens;
    for (;;) {
        Token t = lex.next();
        tokens.push_back(t);
        if (t.type == TokenType::End) break;
    }

    Parser p;
    p.tokens = std::move(tokens);
    int root = p.parse_expr();
    return {std::move(p.nodes), root};
}

}